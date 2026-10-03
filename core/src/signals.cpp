// Original portable pending-signal queues. SPDX-License-Identifier: MIT
#include "artbox/signals.h"
#include "signal_state.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

struct artbox_signals {
    artbox_vm *vm;
    int32_t pid;
    uint32_t uid;
    size_t capacity, waiters = 0;
    std::mutex lock;
    std::vector<artbox_signal_thread*> threads;
    artbox_signal_actions *actions=nullptr;
    artbox_signal_action_validator validate=nullptr;
    void *validation_context=nullptr;
    uint64_t supported_flags=0;
    size_t stack_minimum=0,stack_capacity=0;
};
struct StackRecord { artbox_signal_stack value; StackRecord *next; };
struct artbox_signal_thread {
    artbox_signals *owner;
    artbox_kernel_thread *kernel;
    SignalState state;
    uint64_t wait_mask = 0;
    bool waiting = false;
    std::condition_variable changed;
    const StackRecord disabled{{0,0,2},nullptr};
    std::atomic<const StackRecord*> stack{&disabled};
    StackRecord *stack_records=nullptr;
    size_t stack_records_used=0;
};
static const uint64_t unmaskable = UINT64_C(0x40100);
static uint64_t get64(const unsigned char *bytes) {
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) value|=static_cast<uint64_t>(bytes[i])<<(8*i);
    return value;
}
static void put(unsigned char *bytes,uint64_t value,unsigned size) {
    for (unsigned i=0;i<size;++i) bytes[i]=static_cast<unsigned char>(value>>(8*i));
}
extern "C" artbox_signals *artbox_signals_create(artbox_vm *vm,int32_t pid,uint32_t uid,size_t capacity) {
    if (!vm || pid<=0 || !capacity || capacity>65536) return nullptr;
    artbox_signals *signals=new(std::nothrow) artbox_signals;
    if (!signals) return nullptr;
    signals->vm=vm; signals->pid=pid; signals->uid=uid; signals->capacity=capacity;
    try { signals->threads.reserve(capacity); }
    catch (const std::exception&) { delete signals; return nullptr; }
    return signals;
}
extern "C" int artbox_signals_enable_actions(artbox_signals *signals,size_t capacity,
    artbox_signal_action_validator validate,void *context,uint64_t supported_flags) {
    if(!signals || !validate || !capacity || capacity>65536 || (supported_flags&UINT64_C(0x400))) return -22;
    std::lock_guard<std::mutex> guard(signals->lock);
    if(signals->actions || !signals->threads.empty()) return -16;
    auto *actions=artbox_signal_actions_create(capacity);
    if(!actions) return -12;
    signals->validation_context=context; signals->validate=validate; signals->actions=actions;
    signals->supported_flags=supported_flags;
    return 0;
}
extern "C" artbox_signal_actions *artbox_signals_action_table(artbox_signals *signals) {
    return signals ? signals->actions : nullptr;
}
extern "C" int artbox_signals_mask_snapshot(const artbox_kernel_thread *kernel,uint64_t *mask) {
    if(!kernel || !kernel->signal_state || !mask) return -22;
    *mask=kernel->signal_state->state.mask();
    return 0;
}
extern "C" int artbox_signals_handler_mask_support(void) { return SignalState::handler_safe()?1:0; }
static int change_mask(artbox_signal_thread *thread,uint32_t how,const uint64_t *input,uint64_t *previous) {
    auto old=thread->state.load();
    if(input) {
        const uint64_t requested=*input&~unmaskable;
        for(;;) {
            auto updated=old;
            switch(how) {
                case 0: updated.mask|=requested; break;
                case 1: updated.mask&=~requested; break;
                case 2: updated.mask=requested; break;
                default: return -22;
            }
            if(updated.pending&~updated.mask) return -95;
            if(thread->state.compare_exchange(old,updated)) break;
        }
    }
    if(previous) *previous=old.mask;
    return 0;
}
extern "C" int artbox_signals_mask_update(artbox_kernel_thread *kernel,uint32_t how,
    const uint64_t *input,uint64_t *previous) {
    if(!kernel || !kernel->signal_state) return -22;
    if(!SignalState::handler_safe()) return -95;
    return change_mask(kernel->signal_state,how,input,previous);
}
extern "C" int artbox_signals_enable_stacks(artbox_signals *signals,size_t minimum,size_t capacity) {
    if(!signals || !minimum || !capacity || capacity>65536) return -22;
    std::lock_guard<std::mutex> guard(signals->lock);
    if(signals->stack_minimum || !signals->threads.empty()) return -16;
    signals->stack_minimum=minimum; signals->stack_capacity=capacity;
    return 0;
}
static uint32_t stack_flags(const artbox_signal_stack &stack,uint64_t sp) {
    return !stack.size ? 2u : sp>stack.address && sp-stack.address<=stack.size ? 1u : 0u;
}
extern "C" int artbox_signals_stack_snapshot(const artbox_kernel_thread *kernel,uint64_t sp,
    artbox_signal_stack *output) {
    if(!kernel || !kernel->signal_state || !output) return -22;
    const auto *thread=kernel->signal_state;
    if(!thread->owner->stack_minimum) return -38;
    auto result=thread->stack.load(std::memory_order_acquire)->value;
    result.flags=stack_flags(result,sp);
    *output=result;
    return 0;
}
extern "C" int artbox_signals_stack_update(artbox_kernel_thread *kernel,uint64_t sp,
    const artbox_signal_stack *input,artbox_signal_stack *previous) {
    artbox_signal_stack old;
    int error=artbox_signals_stack_snapshot(kernel,sp,&old);
    if(error) return error;
    auto *thread=kernel->signal_state;
    if(input) {
        if(old.flags==1) return -1;
        auto requested=*input;
        const uint32_t mode=requested.flags&~UINT32_C(0x80000000);
        if(mode!=0 && mode!=1 && mode!=2) return -22;
        if(requested.flags&UINT32_C(0x80000000)) return -95; // SS_AUTODISARM needs return integration.
        if(requested.flags==2) thread->stack.store(&thread->disabled,std::memory_order_release);
        else {
            requested.flags=0; // SS_ONSTACK input is accepted; queries derive the live state.
            if(requested.size<thread->owner->stack_minimum) return -12;
            if(!artbox_vm_access(thread->owner->vm,requested.address,requested.size,3)) return -14;
            const auto current=thread->stack.load(std::memory_order_acquire)->value;
            if(current.address!=requested.address || current.size!=requested.size || current.flags!=requested.flags) {
                if(thread->stack_records_used==thread->owner->stack_capacity) return -12;
                auto *record=new(std::nothrow) StackRecord{requested,thread->stack_records};
                if(!record) return -12;
                thread->stack_records=record; ++thread->stack_records_used;
                thread->stack.store(record,std::memory_order_release);
            }
        }
    }
    if(previous) *previous=old;
    return 0;
}
static int attach(artbox_signals *signals,artbox_kernel_thread *kernel,uint64_t mask) {
    if (!kernel || kernel->vm!=signals->vm || kernel->pid!=signals->pid ||
        kernel->tid<=0 || kernel->signal_state) return -22;
    for (auto *thread:signals->threads) if (thread->kernel->tid==kernel->tid) return -22;
    if (signals->threads.size()==signals->capacity) return -11;
    auto *thread=new(std::nothrow) artbox_signal_thread;
    if (!thread) return -12;
    if (!thread->state.snapshot_lock_free() || !thread->stack.is_lock_free()) { delete thread; return -95; }
    thread->owner=signals; thread->kernel=kernel; thread->state.initialize(mask&~unmaskable);
    signals->threads.push_back(thread); // Capacity was reserved before publication.
    kernel->blocked_signals=thread->state.mask();
    kernel->signal_state=thread;
    return 0;
}
extern "C" int artbox_signals_attach(artbox_signals *signals,artbox_kernel_thread *thread) {
    if (!signals || !thread) return -22;
    std::lock_guard<std::mutex> guard(signals->lock);
    return attach(signals,thread,thread->blocked_signals);
}
extern "C" int artbox_signals_inherit(const artbox_kernel_thread *parent,artbox_kernel_thread *child) {
    if (!parent || !child) return -22;
    if (!parent->signal_state) return 0;
    artbox_signals *signals=parent->signal_state->owner;
    std::lock_guard<std::mutex> guard(signals->lock);
    return attach(signals,child,parent->signal_state->state.mask()); // Pending signals are not inherited.
}
extern "C" int artbox_signals_detach(artbox_kernel_thread *kernel) {
    if (!kernel || !kernel->signal_state) return -22;
    auto *thread=kernel->signal_state;
    auto *signals=thread->owner;
    std::lock_guard<std::mutex> guard(signals->lock);
    if (thread->waiting) return -16;
    signals->threads.erase(std::find(signals->threads.begin(),signals->threads.end(),thread));
    kernel->blocked_signals=thread->state.mask();
    kernel->signal_state=nullptr;
    while(thread->stack_records) {
        auto *next=thread->stack_records->next;
        delete thread->stack_records; thread->stack_records=next;
    }
    delete thread;
    return 0;
}
extern "C" int artbox_signals_destroy(artbox_signals *signals) {
    if (!signals) return -22;
    {
        std::lock_guard<std::mutex> guard(signals->lock);
        if (!signals->threads.empty()) return -16;
    }
    artbox_signal_actions_destroy(signals->actions);
    delete signals; return 0;
}
extern "C" size_t artbox_signals_thread_count(artbox_signals *signals) {
    if (!signals) return 0;
    std::lock_guard<std::mutex> guard(signals->lock);
    return signals->threads.size();
}
extern "C" size_t artbox_signals_waiter_count(artbox_signals *signals) {
    if (!signals) return 0;
    std::lock_guard<std::mutex> guard(signals->lock);
    return signals->waiters;
}
static int64_t alternate_stack(artbox_kernel_thread *kernel,uint64_t in,uint64_t out,uint64_t sp) {
    if(!kernel->signal_state->owner->stack_minimum) return -38;
    unsigned char bytes[24];
    artbox_signal_stack requested{},previous;
    if(in) {
        int error=artbox_vm_read(kernel->vm,in,bytes,sizeof(bytes));
        if(error) return error;
        requested={get64(bytes),get64(bytes+16),static_cast<uint32_t>(get64(bytes+8))};
    }
    int error=artbox_signals_stack_update(kernel,sp,in?&requested:nullptr,&previous);
    if(error || !out) return error;
    put(bytes,previous.address,8); put(bytes+8,previous.flags,8); put(bytes+16,previous.size,8);
    return artbox_vm_write(kernel->vm,out,bytes,sizeof(bytes));
}
static int64_t action(artbox_signal_thread *thread,uint64_t number,uint64_t in,uint64_t out,uint64_t size) {
    auto *owner=thread->owner;
    if(!owner->actions) return -38;
    if(size!=8) return -22;
    unsigned char bytes[32];
    artbox_signal_action requested{},previous{};
    if(in) {
        int error=artbox_vm_read(owner->vm,in,bytes,sizeof(bytes));
        if(error) return error;
        requested={get64(bytes),get64(bytes+8),get64(bytes+16),get64(bytes+24)&~unmaskable};
    }
    unsigned signal=static_cast<uint32_t>(number);
    if(signal<1 || signal>64 || (in && (signal==9 || signal==19))) return -22;
    if(in) {
        if(requested.flags&UINT64_C(0x400)) requested.flags&=owner->supported_flags;
        else if(requested.flags&~owner->supported_flags) return -95;
        int error=owner->validate(owner->validation_context,signal,&requested);
        if(error) return error;
    }
    int error=artbox_signal_actions_set(owner->actions,signal,in?&requested:nullptr,&previous);
    if(error || !out) return error;
    put(bytes,previous.handler,8); put(bytes+8,previous.flags,8);
    put(bytes+16,previous.restorer,8); put(bytes+24,previous.mask,8);
    return artbox_vm_write(owner->vm,out,bytes,sizeof(bytes));
}
static int64_t mask(artbox_signal_thread *thread,uint64_t how,uint64_t in,uint64_t out,uint64_t size) {
    if (size!=8) return -22;
    unsigned char bytes[8];
    uint64_t requested=0,previous;
    if (in) {
        int error=artbox_vm_read(thread->owner->vm,in,bytes,8);
        if (error) return error;
        requested=get64(bytes)&~unmaskable;
    }
    int error=change_mask(thread,static_cast<uint32_t>(how),in?&requested:nullptr,&previous);
    if(error) return error;
    thread->kernel->blocked_signals=thread->state.mask(); // Legacy state outside attachment.
    if (!out) return 0;
    put(bytes,previous,8);
    return artbox_vm_write(thread->owner->vm,out,bytes,8); // Mutation precedes copyout.
}
static int64_t send(artbox_signal_thread *sender,uint64_t process,uint64_t tid,uint64_t number) {
    int32_t pid=static_cast<int32_t>(process),id=static_cast<int32_t>(tid),signal=static_cast<int32_t>(number);
    if (pid<=0 || id<=0) return -22;
    auto *owner=sender->owner;
    std::lock_guard<std::mutex> guard(owner->lock);
    artbox_signal_thread *target=nullptr;
    if (pid==owner->pid) for (auto *thread:owner->threads) if (thread->kernel->tid==id) { target=thread; break; }
    if (!target) return -3;
    if (signal<0 || signal>64) return -22;
    if (!signal) return 0;
    if (signal>=32 || signal==9 || signal==19) return -95;
    uint64_t bit=UINT64_C(1)<<(signal-1);
    auto old=target->state.load();
    for(;;) {
        if(!((old.mask|target->wait_mask)&bit)) return -95;
        auto updated=old; updated.pending|=bit; // Standard signals coalesce.
        if(target->state.compare_exchange(old,updated)) break;
    }
    target->changed.notify_one();
    return 0;
}
static int64_t wait(artbox_signal_thread *thread,uint64_t set,uint64_t info,uint64_t timeout,uint64_t size) {
    if (size!=8) return -22;
    auto *owner=thread->owner;
    unsigned char bytes[16];
    int error=artbox_vm_read(owner->vm,set,bytes,8);
    if (error) return error;
    uint64_t selected=get64(bytes)&~unmaskable;
    int64_t seconds=0,nanoseconds=0;
    if (timeout) {
        error=artbox_vm_read(owner->vm,timeout,bytes,16);
        if (error) return error;
        seconds=static_cast<int64_t>(get64(bytes));
        nanoseconds=static_cast<int64_t>(get64(bytes+8));
        if (seconds<0 || nanoseconds<0 || nanoseconds>=1000000000) return -22;
    }
    std::unique_lock<std::mutex> guard(owner->lock);
    if (thread->waiting) return -16; // API requires a single owner for each guest thread.
    if (!(thread->state.load().pending&selected) && (!timeout || seconds || nanoseconds)) {
        thread->waiting=true; thread->wait_mask=selected; ++owner->waiters;
        auto available=[&] { return (thread->state.load().pending&selected)!=0; };
        if (!timeout) thread->changed.wait(guard,available);
        else {
            using Clock=std::chrono::steady_clock;
            auto now=Clock::now(),deadline=Clock::time_point::max();
            auto remaining=deadline-now;
            if (seconds<std::chrono::duration_cast<std::chrono::seconds>(remaining).count()) {
                deadline=now+std::chrono::seconds(seconds)+std::chrono::nanoseconds(nanoseconds);
            }
            thread->changed.wait_until(guard,deadline,available);
        }
        --owner->waiters; thread->waiting=false; thread->wait_mask=0;
    }
    auto old=thread->state.load();
    unsigned signal;
    for(;;) {
        uint64_t pending=old.pending&selected;
        if(!pending) return -11;
        signal=1;
        while(!(pending&1)) { pending>>=1; ++signal; }
        auto updated=old; updated.pending&=~(UINT64_C(1)<<(signal-1));
        if(thread->state.compare_exchange(old,updated)) break;
    }
    int32_t pid=owner->pid;
    uint32_t uid=owner->uid;
    guard.unlock();
    if (info) {
        unsigned char payload[128]{};
        put(payload,signal,4); put(payload+8,static_cast<uint32_t>(-6),4);
        put(payload+16,static_cast<uint32_t>(pid),4); put(payload+20,uid,4);
        error=artbox_vm_write(owner->vm,info,payload,sizeof(payload));
        if (error) return error; // Linux consumes the pending signal before failed copyout.
    }
    return signal;
}
extern "C" int64_t artbox_signals_call(artbox_kernel_thread *kernel,uint64_t number,
    uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3) {
    if (!kernel || !kernel->signal_state) return -38;
    unsigned char stack_marker;
    switch (number) {
        case 131: return send(kernel->signal_state,a0,a1,a2);
        case 132: return alternate_stack(kernel,a0,a1,(uintptr_t)&stack_marker);
        case 134: return action(kernel->signal_state,a0,a1,a2,a3);
        case 135: return mask(kernel->signal_state,a0,a1,a2,a3);
        case 137: return wait(kernel->signal_state,a0,a1,a2,a3);
        default: return -38;
    }
}
