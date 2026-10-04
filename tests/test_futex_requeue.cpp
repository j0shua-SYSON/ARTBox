// Original paired waiter-movement contract. SPDX-License-Identifier: MIT
#include "artbox/futex.h"
#include "artbox/native_atomic.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#if defined(__linux__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
static artbox_futex *queue;
static bool linux_reference;
static thread_local int guest_errno;
extern "C" int64_t artbox_futex_requeue_check(void*);
static int64_t call(uint64_t address,uint64_t op,uint64_t value=0,uint64_t count=0,
                    uint64_t target=0,uint64_t bits=UINT32_MAX) {
#if defined(__linux__)
    if(linux_reference) {
        long result=syscall(SYS_futex,address,op,value,count,target,bits);
        return result==-1 ? -errno : result;
    }
#endif
    return artbox_futex_call(queue,address,op,value,count,target,bits);
}
extern "C" int *artbox_futex_errno(void) { return &guest_errno; }
extern "C" int64_t artbox_futex_syscall(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    int64_t result=n==98 ? call(a,b,c,d,e,f) : -38;
    if(result<0) { guest_errno=(int)-result; return -1; }
    return result;
}
static bool queued(uint64_t address,int64_t count,bool shared=false) {
    // Same-key requeue with zero wakes observes actual queued Linux waiters
    // without waking them or relying on a sleep/readiness timing assumption.
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    do {
        int64_t observed=shared && !linux_reference ? (int64_t)artbox_futex_waiters(queue,address,0) :
            call(address,shared ? 3 : 131,0,INT32_MAX,address);
        if(observed==count) return true;
        std::this_thread::yield();
    } while(std::chrono::steady_clock::now()<end);
    return false;
}
static void exercise(bool reference) {
    linux_reference=reference;
    auto memory=artbox_native_vm(); auto atomic=artbox_native_atomic_u32(); auto system=artbox_native_system();
    auto *vm=artbox_vm_create(&memory,memory.page_size,4);
    CHECK(vm);
    int64_t mapped=artbox_vm_mmap(vm,0,memory.page_size,3,0x22,-1,0);
    CHECK(mapped>0);
    const uint64_t source=(uint64_t)mapped,target=source+8,timeout=source+32;
    queue=artbox_futex_create(vm,&atomic,&system,8);
    CHECK(queue);
    int64_t wire=artbox_futex_requeue_check((void *)(uintptr_t)source);
    if(wire!=18) std::fprintf(stderr,"requeue wire caller: %lld\n",(long long)wire);
    CHECK(wire==18);
    CHECK(artbox_vm_store_u32(vm,source,&atomic,0)==0);
    CHECK(artbox_vm_store_u32(vm,target,&atomic,77)==0); // Destination value must not be read.
    auto deadline=[&](int milliseconds) {
        artbox_timespec value;
        CHECK(system.clock(1,&value)==0);
        value.seconds+=milliseconds/1000;
        value.nanoseconds+=(milliseconds%1000)*1000000;
        if(value.nanoseconds>=1000000000) { ++value.seconds; value.nanoseconds-=1000000000; }
        CHECK(artbox_vm_write(vm,timeout,&value,sizeof(value))==0);
    };
    std::atomic<int64_t> first{99},second{99},shared{99};
    deadline(3000);
    std::thread one([&] { first=call(source,137,0,timeout,0,1); });
    std::thread two([&] { second=call(source,137,0,timeout,0,2); });
    std::thread other([&] { shared=call(source,9,0,timeout,0,4); });
    CHECK(queued(source,2) && queued(source,1,true));
    CHECK(call(source,131,0,0,target)==0 && queued(source,2));
    CHECK(first==99 && second==99); // Zero wake/requeue is a real no-op.
    CHECK(call(source,131,0,INT32_MAX,target)==2);
    CHECK(call(source,129,INT32_MAX)==0 && queued(target,2));
    CHECK(call(target,138,1,0,0,1)==1);
    one.join(); CHECK(first==0 && second==99);
    CHECK(call(target,138,1,0,0,2)==1);
    two.join(); CHECK(second==0);
    CHECK(shared==99 && call(source,1,1)==1);
    other.join(); CHECK(shared==0);
    for(unsigned wake=0;wake<=1;++wake) {
        first=second=99; deadline(3000);
        std::thread a([&] { first=call(source,137,0,timeout,0,1); });
        std::thread b([&] { second=call(source,137,0,timeout,0,2); });
        CHECK(queued(source,2));
        CHECK(call(source,131,wake,1,target)==wake+1);
        CHECK(call(source,129,INT32_MAX)==1-wake);
        CHECK(call(target,129,INT32_MAX)==1);
        a.join(); b.join(); CHECK(first==0 && second==0);
    }
    // Moving a timed waiter must preserve its original absolute deadline.
    first=99; deadline(1000);
    std::thread timed([&] { first=call(source,137,0,timeout,0,1); });
    CHECK(queued(source,1) && call(source,131,0,1,target)==1);
    timed.join(); CHECK(first==-110 && call(target,129,1)==0);
    // Race insertion with relocation and waking either key. Every waiter is
    // moved at most once and completes; no notification is lost between keys.
    for(unsigned i=0;i<128;++i) {
        first=99; deadline(3000);
        std::thread waiter([&] { first=call(source,137,0,timeout,0,1); });
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        int64_t moved=0,woken=0;
        while(!woken) {
            int64_t n=call(source,131,0,1,target);
            CHECK(n==0 || n==1); moved+=n; CHECK(moved<=1);
            woken=call(target,129,1)+call(source,129,1);
            CHECK(woken==0 || woken==1);
            CHECK(std::chrono::steady_clock::now()<limit); std::this_thread::yield();
        }
        waiter.join(); CHECK(first==0);
    }
    CHECK(artbox_futex_destroy(queue)==0 && artbox_vm_destroy(vm)==0);
    std::printf("%s: 18 wire cases, movement, masks, isolation, timeout and 128 requeue/wake races pass\n",
        reference ? "Linux kernel" : "Portable futex");
}
int main() {
    exercise(false);
#if defined(__linux__)
    exercise(true);
#endif
}
