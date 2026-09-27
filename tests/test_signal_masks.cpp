// Original coherent mask/queue transition tests. SPDX-License-Identifier: MIT
#include "artbox/signals.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"mask line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static const uint64_t bit=UINT64_C(1)<<9,unmaskable=UINT64_C(0x40100);
struct Buffers { uint64_t input,old,set,timeout[2]; };
static int64_t ordinary(artbox_kernel_thread *thread,Buffers *b,uint32_t how,const uint64_t *input) {
    if(input) b->input=*input;
    return artbox_kernel_call(thread,135,how,input?(uintptr_t)&b->input:0,(uintptr_t)&b->old,8,0,0);
}
static int64_t update_under_mapper_lock(void *context,void *,size_t length) {
    auto *thread=static_cast<artbox_kernel_thread*>(context);
    uint64_t input=bit,previous=77,current=0;
    int changed=artbox_signals_mask_update(thread,2,&input,&previous);
    CHECK(changed==(artbox_signals_handler_mask_support()?0:-95));
    CHECK(!artbox_signals_mask_snapshot(thread,&current) && current==bit);
    CHECK(previous==(artbox_signals_handler_mask_support()?bit:77));
    return static_cast<int64_t>(length);
}
int main() {
    auto memory=artbox_native_vm(); auto system=artbox_native_system();
    auto *vm=artbox_vm_create(&memory,1024*1024,16);
    artbox_kernel_thread owner,sender;
    CHECK(vm && !artbox_kernel_thread_init(&owner,vm,&system,100,100));
    CHECK(!artbox_kernel_thread_init(&sender,vm,&system,100,101));
    auto *signals=artbox_signals_create(vm,100,10000,2);
    CHECK(signals && !artbox_signals_attach(signals,&owner) && !artbox_signals_attach(signals,&sender));
    int64_t address=artbox_vm_mmap(vm,0,memory.page_size,3,0x22,-1,0);
    CHECK(address>0);
    auto *b=reinterpret_cast<Buffers*>(static_cast<uintptr_t>(address));
    *b=Buffers{}; b->set=bit;
    const bool fast=artbox_signals_handler_mask_support()!=0;
#if defined(__APPLE__) && defined(__aarch64__)
    CHECK(fast); // Apple delivery must never silently fall back to handler locks.
#endif
    uint64_t all=UINT64_MAX,zero=0,old=77,seen=0;
    CHECK(!ordinary(&owner,b,2,&all) && b->old==0);
    CHECK(!artbox_signals_mask_snapshot(&owner,&seen) && seen==(all&~unmaskable));
    if(fast) {
        CHECK(!artbox_signals_mask_update(&owner,1,&all,&old) && old==(all&~unmaskable));
        CHECK(!artbox_signals_mask_update(&owner,0,&bit,&old) && old==0);
        CHECK(!artbox_signals_mask_update(&owner,99,nullptr,&old) && old==bit);
        old=77;
        CHECK(artbox_signals_mask_update(&owner,99,&zero,&old)==-22 && old==77);
        CHECK(!artbox_signals_mask_snapshot(&owner,&seen) && seen==bit);
    } else CHECK(artbox_signals_mask_update(&owner,2,&zero,&old)==-95 && old==77);
    CHECK(!ordinary(&owner,b,2,&bit));
    CHECK(artbox_vm_transfer(vm,static_cast<uint64_t>(address),1,1,update_under_mapper_lock,&owner)==1);
    CHECK(!artbox_kernel_call(&sender,131,100,100,10,0,0,0));
    old=77;
    if(fast) CHECK(artbox_signals_mask_update(&owner,2,&zero,&old)==-95 && old==77);
    CHECK(ordinary(&owner,b,2,&zero)==-95);
    CHECK(!artbox_signals_mask_snapshot(&owner,&seen) && seen==bit);
    CHECK(artbox_kernel_call(&owner,137,(uintptr_t)&b->set,0,(uintptr_t)&b->timeout,8,0,0)==10);

    // The sender and handler unmask contend on one logical state. A successful
    // enqueue prevents unmask; a successful unmask prevents blocked-only send.
    std::atomic<unsigned> go{0},done{0};
    std::atomic<int64_t> sent{0};
    std::thread worker([&] {
        for(unsigned n=1;n<=4096;++n) {
            while(go.load(std::memory_order_acquire)!=n) std::this_thread::yield();
            sent.store(artbox_kernel_call(&sender,131,100,100,10,0,0,0),std::memory_order_relaxed);
            done.store(n,std::memory_order_release);
        }
    });
    for(unsigned n=1;n<=4096;++n) {
        CHECK(!ordinary(&owner,b,2,&bit));
        go.store(n,std::memory_order_release);
        int64_t changed=fast?artbox_signals_mask_update(&owner,1,&bit,&old):ordinary(&owner,b,1,&bit);
        while(done.load(std::memory_order_acquire)!=n) std::this_thread::yield();
        int64_t queued=sent.load(std::memory_order_relaxed);
        CHECK((changed==0 && queued==-95) || (changed==-95 && queued==0));
        CHECK(!artbox_signals_mask_snapshot(&owner,&seen) && seen==(queued==0?bit:0));
        CHECK(artbox_kernel_call(&owner,137,(uintptr_t)&b->set,0,(uintptr_t)&b->timeout,8,0,0)==(queued==0?10:-11));
    }
    worker.join();
    CHECK(!ordinary(&owner,b,2,&zero));
    CHECK(!artbox_signals_detach(&sender) && !artbox_signals_detach(&owner) && !artbox_signals_destroy(signals));
    CHECK(!artbox_vm_munmap(vm,static_cast<uint64_t>(address),memory.page_size) && !artbox_vm_destroy(vm));
    std::printf("4096 enqueue/unmask races preserve pending state; handler mask support: %s\n",fast?"lock-free":"unavailable");
}
