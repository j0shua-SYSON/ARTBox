// Original portable pending-signal queues. SPDX-License-Identifier: MIT
#include "artbox/signals.h"
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
};
struct artbox_signal_thread {
    artbox_signals *owner;
    artbox_kernel_thread *kernel;
    std::atomic<uint64_t> mask{0};
    uint64_t pending = 0, wait_mask = 0;
    bool waiting = false;
    std::condition_variable changed;
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
    artbox_signal_action_validator validate,void *context) {
    if(!signals || !validate || !capacity || capacity>65536) return -22;
    std::lock_guard<std::mutex> guard(signals->lock);
    if(signals->actions || !signals->threads.empty()) return -16;
    auto *actions=artbox_signal_actions_create(capacity);
    if(!actions) return -12;
    signals->validation_context=context; signals->validate=validate; signals->actions=actions;
    return 0;
}
extern "C" artbox_signal_actions *artbox_signals_action_table(artbox_signals *signals) {
    return signals ? signals->actions : nullptr;
}
extern "C" int artbox_signals_mask_snapshot(const artbox_kernel_thread *kernel,uint64_t *mask) {
    if(!kernel || !kernel->signal_state || !mask) return -22;
    *mask=kernel->signal_state->mask.load(std::memory_order_acquire);
    return 0;
}
static int attach(artbox_signals *signals,artbox_kernel_thread *kernel,uint64_t mask) {
    if (!kernel || kernel->vm!=signals->vm || kernel->pid!=signals->pid ||
        kernel->tid<=0 || kernel->signal_state) return -22;
    for (auto *thread:signals->threads) if (thread->kernel->tid==kernel->tid) return -22;
    if (signals->threads.size()==signals->capacity) return -11;
    auto *thread=new(std::nothrow) artbox_signal_thread;
    if (!thread) return -12;
    if (!thread->mask.is_lock_free()) { delete thread; return -95; }
    thread->owner=signals; thread->kernel=kernel; thread->mask=mask&~unmaskable;
    signals->threads.push_back(thread); // Capacity was reserved before publication.
    kernel->blocked_signals=thread->mask;
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
    return attach(signals,child,parent->signal_state->mask); // Pending signals are not inherited.
}
extern "C" int artbox_signals_detach(artbox_kernel_thread *kernel) {
    if (!kernel || !kernel->signal_state) return -22;
    auto *thread=kernel->signal_state;
    auto *signals=thread->owner;
    std::lock_guard<std::mutex> guard(signals->lock);
    if (thread->waiting) return -16;
    signals->threads.erase(std::find(signals->threads.begin(),signals->threads.end(),thread));
    kernel->blocked_signals=thread->mask;
    kernel->signal_state=nullptr;
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
    {
        std::lock_guard<std::mutex> guard(thread->owner->lock);
        previous=thread->mask;
        if (in) {
            uint64_t updated;
            switch (static_cast<uint32_t>(how)) {
                case 0: updated=previous|requested; break;
                case 1: updated=previous&~requested; break;
                case 2: updated=requested; break;
                default: return -22;
            }
            // No false success for a transition that requires unblocked
            // handler/default delivery, which is not implemented yet.
            if (thread->pending&~updated) return -95;
            thread->mask=updated;
            thread->kernel->blocked_signals=updated;
        }
    }
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
    if (!((target->mask|target->wait_mask)&bit)) return -95;
    target->pending|=bit; // Standard signals coalesce; one process has one sender PID/UID.
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
    if (!(thread->pending&selected) && (!timeout || seconds || nanoseconds)) {
        thread->waiting=true; thread->wait_mask=selected; ++owner->waiters;
        auto available=[&] { return (thread->pending&selected)!=0; };
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
    uint64_t pending=thread->pending&selected;
    if (!pending) return -11;
    unsigned signal=1;
    while (!(pending&1)) { pending>>=1; ++signal; }
    thread->pending&=~(UINT64_C(1)<<(signal-1));
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
    switch (number) {
        case 131: return send(kernel->signal_state,a0,a1,a2);
        case 134: return action(kernel->signal_state,a0,a1,a2,a3);
        case 135: return mask(kernel->signal_state,a0,a1,a2,a3);
        case 137: return wait(kernel->signal_state,a0,a1,a2,a3);
        default: return -38;
    }
}
