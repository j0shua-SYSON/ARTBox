// Original queued interruption and lifetime contract. SPDX-License-Identifier: MIT
#include "artbox/signals.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include "artbox/native_atomic.h"
#include "artbox/futex.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(0)
static artbox_kernel_thread *caller;
extern "C" int64_t artbox_signal_realtime_check(uint64_t,uint64_t,uint64_t,uint32_t);
extern "C" int64_t artbox_signal_syscall(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    return artbox_kernel_call(caller,n,a,b,c,d,e,f);
}
static int64_t call(artbox_kernel_thread &t,uint64_t n,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0) {
    return artbox_kernel_call(&t,n,a,b,c,d,0,0);
}
static void notify(void *raw) { static_cast<std::atomic<unsigned>*>(raw)->fetch_add(1); }
int main() {
    auto memory=artbox_native_vm(); auto system=artbox_native_system();
    auto *vm=artbox_vm_create(&memory,8*1024*1024,16);
    artbox_kernel_thread main,child;
    CHECK(vm && artbox_kernel_thread_init(&main,vm,&system,100,100)==0);
    CHECK(artbox_kernel_thread_init(&child,vm,&system,100,101)==0);
    auto *signals=artbox_signals_create(vm,100,10000,4);
    CHECK(signals && artbox_signals_enable_interrupt(signals,31,4)==-22);
    CHECK(artbox_signals_enable_interrupt(signals,65,4)==-22);
    CHECK(artbox_signals_enable_interrupt(signals,34,0)==-22);
    CHECK(artbox_signals_enable_interrupt(signals,34,4)==0);
    CHECK(artbox_signals_enable_interrupt(signals,34,4)==-16);
    CHECK(artbox_signals_attach(signals,&main)==0);
    CHECK(artbox_signals_interrupt_number(&main)==34);
    caller=&main;
    const int64_t cases=artbox_signal_realtime_check(memory.page_size,100,100,10000);
    if(cases!=26) std::fprintf(stderr,"realtime fixture: %lld\n",(long long)cases);
    CHECK(cases==26);
    const uint64_t buffer=(uint64_t)artbox_vm_mmap(vm,0,memory.page_size,3,0x22,-1,0);
    CHECK(buffer<INT64_MAX);
    auto *words=(uint64_t *)(uintptr_t)buffer;
    words[0]=UINT64_C(1)<<33; words[1]=words[2]=0;
    CHECK(call(main,131,100,100,34)==-95); // No unblocked owner yet.
    CHECK(call(main,135,0,buffer,0,8)==0);
    for(int i=0;i<4;++i) CHECK(call(main,131,100,100,34)==0);
    CHECK(call(main,131,100,100,34)==-11); // No coalescing on quota exhaustion.
    CHECK(call(main,135,1,buffer,0,8)==-95);
    CHECK(artbox_signals_inherit(&main,&child)==0);
    CHECK(call(child,137,buffer,0,buffer+8,8)==-11); // Mask, not pending count, inherited.
    for(int i=0;i<4;++i) CHECK(call(main,137,buffer,0,buffer+8,8)==34);
    CHECK(call(main,137,buffer,0,buffer+8,8)==-11);
    std::atomic<unsigned> notifications{0};
    CHECK(artbox_signals_bind_interrupt(&main,notify,&notifications)==0);
    CHECK(artbox_signals_bind_interrupt(&main,notify,&notifications)==-16);
    CHECK(call(main,131,100,100,34)==0 && notifications==0);
    CHECK(call(main,135,1,buffer,0,8)==0 && notifications==1);
    uint64_t saved=UINT64_MAX,mask=0;
    CHECK(artbox_signals_take_interrupt(&main,UINT64_C(0x200)|UINT64_C(0x40100),&saved)==34 && saved==0);
    CHECK(artbox_signals_mask_snapshot(&main,&mask)==0 && mask==(words[0]|UINT64_C(0x200)));
    CHECK(artbox_signals_interrupt_epoch(&main)==1);
    CHECK(artbox_signals_take_interrupt(&main,0,&saved)==0 && saved==0);
    CHECK(call(main,131,100,100,34)==0 && notifications==1); // Queue during handler mask.
    words[3]=saved;
    CHECK(call(main,135,2,buffer+24,0,8)==0 && notifications==2);
    CHECK(artbox_signals_take_interrupt(&main,0,&saved)==34);
    CHECK(call(main,135,2,buffer+24,0,8)==0);
    // Concurrent publication and owning consumption must retain every accepted send.
    std::atomic<unsigned> sent{0}; std::atomic<bool> finished{false};
    std::thread producer([&] {
        for(unsigned i=0;i<4096;++i) {
            int64_t result;
            do { result=call(child,131,100,100,34); if(result==-11) std::this_thread::yield(); } while(result==-11);
            CHECK(result==0); ++sent;
        }
        finished=true;
    });
    unsigned received=0;
    while(!finished || received!=sent) {
        int number=artbox_signals_take_interrupt(&main,0,&saved);
        CHECK(number==0 || number==34);
        if(number) {
            ++received; words[3]=saved;
            CHECK(call(main,135,2,buffer+24,0,8)==0);
        } else std::this_thread::yield();
    }
    producer.join(); CHECK(received==4096 && artbox_signals_interrupt_epoch(&main)==4098);
    auto atomic=artbox_native_atomic_u32();
    auto *futex=artbox_futex_create(vm,&atomic,&system,4);
    CHECK(futex);
    words[7]=0;
    std::atomic<int64_t> wait_result{99};
    std::thread waiting([&] {
        wait_result=artbox_futex_call_interruptible(futex,&main,buffer+56,128,0,0,0,0);
    });
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!artbox_futex_waiters(futex,buffer+56,1)) {
        CHECK(std::chrono::steady_clock::now()<deadline); std::this_thread::yield();
    }
    CHECK(call(child,131,100,100,34)==0);
    CHECK(artbox_signals_take_interrupt(&main,0,&saved)==34);
    words[3]=saved;
    CHECK(call(main,135,2,buffer+24,0,8)==0);
    waiting.join();
    CHECK(wait_result==-4 && artbox_futex_destroy(futex)==0);
    // An unselected delivered signal interrupts sigtimedwait too, without
    // touching its condition variable from the simulated handler.
    words[6]=UINT64_C(1)<<9;
    wait_result=99;
    std::thread signal_waiting([&] { wait_result=call(main,137,buffer+48,0,0,8); });
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!artbox_signals_waiter_count(signals)) {
        CHECK(std::chrono::steady_clock::now()<deadline); std::this_thread::yield();
    }
    CHECK(call(child,131,100,100,34)==0);
    CHECK(artbox_signals_take_interrupt(&main,0,&saved)==34);
    words[3]=saved;
    CHECK(call(main,135,2,buffer+24,0,8)==0);
    signal_waiting.join(); CHECK(wait_result==-4);
    CHECK(artbox_signals_detach(&main)==-16); // Native callback lifetime must end first.
    CHECK(artbox_signals_bind_interrupt(&main,nullptr,nullptr)==0);
    CHECK(call(main,131,100,100,34)==-95);
    CHECK(artbox_signals_detach(&child)==0 && call(main,131,100,101,34)==-3);
    CHECK(artbox_signals_detach(&main)==0 && artbox_signals_destroy(signals)==0);
    CHECK(artbox_vm_munmap(vm,buffer,memory.page_size)==0 && artbox_vm_destroy(vm)==0);
    std::puts("26 realtime ABI cases, quota, mask restoration, lifetime, 4096 deliveries and interrupted waits pass");
}
