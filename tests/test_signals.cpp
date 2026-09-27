// Original ARTBox signal queue/lifetime contract. SPDX-License-Identifier: MIT
#include "artbox/signals.h"
#include "artbox/threads.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include "artbox/native_atomic.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while (0)
static artbox_kernel_thread *main_thread;
extern "C" int64_t artbox_signal_wait_check(uint64_t, uint64_t, uint64_t, uint32_t);
extern "C" int64_t artbox_signal_syscall(uint64_t n, uint64_t a, uint64_t b, uint64_t c,
                                        uint64_t d, uint64_t e, uint64_t f) {
    return artbox_kernel_call(main_thread,n,a,b,c,d,e,f);
}
static int64_t call(artbox_kernel_thread &t, uint64_t n, uint64_t a=0, uint64_t b=0, uint64_t c=0, uint64_t d=0) {
    return artbox_kernel_call(&t,n,a,b,c,d,0,0);
}
static bool reject_start;
static int validate_action(void *,unsigned number,const artbox_signal_action *action) {
    if(number!=5 || action->flags&~UINT64_C(4) || action->restorer || action->mask) return -95;
    return !action->handler || (action->handler>=0x1000 && action->handler<0x2000 && !(action->handler&3)) ? 0 : -22;
}
static int start(void *,size_t,void (*entry)(void *),void *argument,void **handle) {
    if (reject_start) return -11;
    *handle = new std::thread(entry,argument); return 0;
}
static int join(void *handle) {
    auto *thread = static_cast<std::thread*>(handle); thread->join(); delete thread; return 0;
}
struct Worker { uint64_t buffer; std::atomic<int64_t> result{0}; };
static void run(void *context,artbox_kernel_thread *thread,const artbox_thread_start *,artbox_thread_finish *) {
    Worker *worker=static_cast<Worker*>(context);
    CHECK(call(*thread,137,worker->buffer+48,0,worker->buffer+16,8)==-11); // Parent pending set is not inherited.
    worker->result=call(*thread,137,worker->buffer,worker->buffer+64,0,8);
}
int main() {
    artbox_vm_ops memory=artbox_native_vm();
    artbox_vm *vm=artbox_vm_create(&memory,8*1024*1024,16);
    artbox_system_ops system=artbox_native_system();
    artbox_kernel_thread parent;
    CHECK(vm && artbox_kernel_thread_init(&parent,vm,&system,100,100)==0);
    main_thread=&parent;
    artbox_signals *signals=artbox_signals_create(vm,100,10000,4);
    CHECK(signals && artbox_signals_enable_actions(signals,8,validate_action,nullptr)==0);
    CHECK(signals && artbox_signals_attach(signals,&parent)==0);
    CHECK(artbox_signals_enable_actions(signals,8,validate_action,nullptr)==-16);
    CHECK(artbox_signals_attach(signals,&parent)==-22);
    artbox_kernel_thread duplicate;
    CHECK(artbox_kernel_thread_init(&duplicate,vm,&system,100,100)==0);
    CHECK(artbox_signals_attach(signals,&duplicate)==-22);
    CHECK(artbox_signals_destroy(signals)==-16);
    int64_t result=artbox_signal_wait_check(memory.page_size,100,100,10000);
    if (result!=33) std::fprintf(stderr,"signal fixture result: %lld\n",static_cast<long long>(result));
    CHECK(result==33);
    uint64_t buffer=static_cast<uint64_t>(artbox_vm_mmap(vm,0,memory.page_size,3,0x22,-1,0));
    CHECK(buffer<INT64_MAX);
    auto *words=reinterpret_cast<uint64_t*>(buffer);
    const artbox_signal_action action{0x1000,4,0,0};
    CHECK(artbox_vm_write(vm,buffer+1,&action,sizeof(action))==0);
    CHECK(call(parent,134,5,buffer+1,buffer+65,8)==0); // Unaligned Linux copyin/out.
    artbox_signal_action observed;
    CHECK(artbox_vm_read(vm,buffer+65,&observed,sizeof(observed))==0 && observed.handler==0);
    CHECK(call(parent,134,5,0,buffer+65,8)==0);
    CHECK(artbox_vm_read(vm,buffer+65,&observed,sizeof(observed))==0 && observed.handler==0x1000);
    CHECK(call(parent,134,5,1,0,0)==-22); // Size precedes copyin.
    CHECK(call(parent,134,0,1,0,8)==-14); // Copyin precedes signal validation.
    CHECK(call(parent,134,0,0,0,8)==-22);
    CHECK(call(parent,134,65,0,0,8)==-22);
    CHECK(call(parent,134,9,buffer+1,0,8)==-22);
    CHECK(call(parent,134,19,buffer+1,0,8)==-22);
    CHECK(call(parent,134,9,0,buffer+65,8)==0);
    CHECK(call(parent,134,11,buffer+1,0,8)==-95); // No delivery owner for SIGSEGV yet.
    observed=action; observed.handler=0x3000;
    CHECK(artbox_vm_write(vm,buffer+1,&observed,sizeof(observed))==0);
    CHECK(call(parent,134,5,buffer+1,0,8)==-22);
    CHECK(artbox_signal_actions_snapshot(artbox_signals_action_table(signals),5,&observed)==0 && observed.handler==0x1000);
    observed=action; observed.handler=0x1004;
    CHECK(artbox_vm_write(vm,buffer+1,&observed,sizeof(observed))==0);
    CHECK(call(parent,134,5,buffer+1,1,8)==-14); // Publication precedes copyout failure.
    CHECK(artbox_signal_actions_snapshot(artbox_signals_action_table(signals),5,&observed)==0 && observed.handler==0x1004);
    words[0]=UINT64_C(0x200);
    words[1]=UINT64_MAX;
    CHECK(call(parent,135,2,buffer+8,1,8)==-14);
    CHECK(call(parent,135,999,0,buffer+8,8)==0 && words[1]==(UINT64_MAX&~UINT64_C(0x40100)));
    CHECK(call(parent,135,2,buffer,0,8)==0);
    CHECK(call(parent,135,0,buffer,0,8)==0);
    CHECK(call(parent,131,100,100,12)==-95); // No unblocked/default delivery claim.
    CHECK(call(parent,131,100,100,32)==-95); // Realtime queue not implemented.
    CHECK(call(parent,131,100,100,9)==-95);
    CHECK(call(parent,131,100,100,10)==0);
    CHECK(call(parent,135,1,buffer,0,8)==-95); // Preserve pending signal until a handler bridge exists.
    words[2]=words[3]=0;
    CHECK(call(parent,137,buffer,0,buffer+16,8)==10);
    words[3]=1000000;
    const auto timeout_start=std::chrono::steady_clock::now();
    CHECK(call(parent,137,buffer,0,buffer+16,8)==-11);
    CHECK(std::chrono::steady_clock::now()-timeout_start>=std::chrono::microseconds(500));
    words[3]=0;
    words[6]=UINT64_C(0x800);
    CHECK(call(parent,135,0,buffer+48,0,8)==0);
    CHECK(call(parent,131,100,100,12)==0);
    artbox_atomic_u32_ops atomic=artbox_native_atomic_u32();
    artbox_futex *futex=artbox_futex_create(vm,&atomic,&system,8);
    artbox_thread_ops native{start,join};
    Worker worker; worker.buffer=buffer;
    artbox_threads *threads=artbox_threads_create(vm,futex,&atomic,&system,&native,100,101,2,run,&worker);
    uint64_t stack=static_cast<uint64_t>(artbox_vm_mmap(vm,0,memory.page_size,3,0x22,-1,0));
    CHECK(futex && threads && stack<INT64_MAX);
    artbox_thread_start request{ARTBOX_PTHREAD_CLONE_FLAGS,stack,memory.page_size,123,buffer+32,buffer+32,456,789};
    reject_start=true;
    CHECK(artbox_threads_start(threads,&parent,&request)==-11 && artbox_signals_thread_count(signals)==1);
    reject_start=false;
    int64_t tid=artbox_threads_start(threads,&parent,&request);
    CHECK(tid>100);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (artbox_signals_waiter_count(signals)!=1) {
        CHECK(std::chrono::steady_clock::now()<end); std::this_thread::yield();
    }
    CHECK(artbox_signals_thread_count(signals)==2);
    CHECK(call(parent,131,100,static_cast<uint64_t>(tid),0)==0);
    CHECK(call(parent,131,100,static_cast<uint64_t>(tid),10)==0);
    CHECK(artbox_threads_drain(threads,3000)==0 && worker.result==10);
    CHECK(artbox_threads_reaped(threads)==1 && artbox_signals_thread_count(signals)==1);
    CHECK(call(parent,131,100,static_cast<uint64_t>(tid),0)==-3);
    CHECK(call(parent,137,buffer+48,0,buffer+16,8)==12); // Parent pending signal survived clone and child exit.
    CHECK(*reinterpret_cast<uint32_t*>(buffer+32)==0);
    CHECK(artbox_threads_destroy(threads)==0 && artbox_futex_destroy(futex)==0);
    CHECK(artbox_signals_detach(&parent)==0 && artbox_signals_thread_count(signals)==0);
    CHECK(artbox_signals_destroy(signals)==0);
    CHECK(artbox_vm_munmap(vm,buffer,memory.page_size)==0 && artbox_vm_munmap(vm,stack,memory.page_size)==0);
    CHECK(artbox_vm_destroy(vm)==0);
    std::puts("33 signal ABI cases, inherited mask, blocked wakeup and TID lifetime pass");
}
