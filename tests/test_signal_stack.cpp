// Original alternate-stack publication/lifetime tests. SPDX-License-Identifier: MIT
#include "artbox/signals.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"stack line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static artbox_kernel_thread *current;
extern "C" int64_t artbox_signal_stack_check(uint64_t);
extern "C" int64_t artbox_signal_syscall(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    return artbox_kernel_call(current,n,a,b,c,d,e,f);
}
int main() {
    auto memory=artbox_native_vm(); auto system=artbox_native_system();
    auto *vm=artbox_vm_create(&memory,1024*1024,16);
    artbox_kernel_thread thread,child;
    CHECK(vm && !artbox_kernel_thread_init(&thread,vm,&system,100,100));
    CHECK(!artbox_kernel_thread_init(&child,vm,&system,100,101));
    auto *signals=artbox_signals_create(vm,100,10000,2);
    CHECK(signals && !artbox_signals_enable_stacks(signals,8192,4096));
    CHECK(!artbox_signals_attach(signals,&thread)); current=&thread;
    CHECK(artbox_signals_enable_stacks(signals,8192,4096)==-16);
    CHECK(artbox_signal_stack_check(memory.page_size)==17);
    int64_t mapped=artbox_vm_mmap(vm,0,32768,3,0x22,-1,0);
    CHECK(mapped>0);
    artbox_signal_stack input{static_cast<uint64_t>(mapped),32768,0},old{},observed{};
    CHECK(!artbox_signals_stack_update(&thread,0,&input,&old) && old.flags==2);
    CHECK(!artbox_signals_stack_snapshot(&thread,input.address,&observed) && observed.flags==0);
    CHECK(!artbox_signals_stack_snapshot(&thread,input.address+1,&observed) && observed.flags==1);
    CHECK(!artbox_signals_stack_snapshot(&thread,input.address+input.size,&observed) && observed.flags==1);
    CHECK(!artbox_signals_stack_snapshot(&thread,input.address+input.size+1,&observed) && observed.flags==0);
    old={9,10,11}; input.flags=0x40000000;
    CHECK(artbox_signals_stack_update(&thread,input.address+16,&input,&old)==-1 && old.address==9); // EPERM precedes flags.
    CHECK(artbox_signals_stack_update(&thread,0,&input,&old)==-22 && old.address==9);
    input.flags=0x80000000;
    CHECK(artbox_signals_stack_update(&thread,0,&input,&old)==-95);
    input.flags=0xc0000000;
    CHECK(artbox_signals_stack_update(&thread,0,&input,&old)==-22);
    input.flags=0; input.size=8191;
    CHECK(artbox_signals_stack_update(&thread,0,&input,&old)==-12 && old.address==9);
    input.address=1; input.size=8192;
    CHECK(artbox_signals_stack_update(&thread,0,&input,&old)==-14);
    CHECK(!artbox_signals_inherit(&thread,&child));
    CHECK(!artbox_signals_stack_snapshot(&child,static_cast<uint64_t>(mapped)+16,&observed) && observed.flags==2);
    CHECK(!artbox_signals_detach(&child));
    std::atomic<bool> start{false},finished{false};
    std::thread writer([&] {
        while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for(unsigned n=0;n<2048;++n) {
            const uint64_t offset=16*(n%32);
            artbox_signal_stack value{static_cast<uint64_t>(mapped)+offset,32768-offset,0};
            CHECK(!artbox_signals_stack_update(&thread,0,&value,nullptr));
        }
        finished.store(true,std::memory_order_release);
    });
    start.store(true,std::memory_order_release);
    do {
        CHECK(!artbox_signals_stack_snapshot(&thread,0,&observed) && !observed.flags &&
            observed.address+observed.size==static_cast<uint64_t>(mapped)+32768);
    } while(!finished.load(std::memory_order_acquire));
    writer.join();
    CHECK(!artbox_signals_detach(&thread) && !artbox_signals_destroy(signals));
    signals=artbox_signals_create(vm,100,10000,1);
    CHECK(signals && !artbox_signals_enable_stacks(signals,8192,1) && !artbox_signals_attach(signals,&thread));
    input={static_cast<uint64_t>(mapped),8192,0};
    CHECK(!artbox_signals_stack_update(&thread,0,&input,nullptr));
    CHECK(!artbox_signals_stack_update(&thread,0,&input,nullptr)); // Idempotence consumes no record.
    input.size=16384; old={9,10,11};
    CHECK(artbox_signals_stack_update(&thread,0,&input,&old)==-12 && old.address==9);
    CHECK(!artbox_signals_stack_snapshot(&thread,0,&observed) && observed.size==8192);
    input.flags=2;
    CHECK(!artbox_signals_stack_update(&thread,0,&input,&old) && old.size==8192);
    CHECK(!artbox_signals_stack_snapshot(&thread,0,&observed) && observed.flags==2);
    CHECK(!artbox_signals_detach(&thread) && !artbox_signals_destroy(signals));
    CHECK(!artbox_vm_munmap(vm,static_cast<uint64_t>(mapped),32768) && !artbox_vm_destroy(vm));
    std::puts("17 Linux stack ABI cases, active bounds, clone reset and concurrent publication pass");
}
