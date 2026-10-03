// Original immutable signal-action publication test. SPDX-License-Identifier: MIT
#include "artbox/signal_actions.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <thread>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"action line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
int main() {
    const artbox_signal_action zero{},first{0x1234,4,0,UINT64_MAX},second{0x5678,0x10000004,9,0};
    artbox_signal_action observed{},unchanged{7,8,9,10};
    auto *actions=artbox_signal_actions_create(2);
    CHECK(actions && !artbox_signal_actions_create(0));
    CHECK(artbox_signal_actions_snapshot(actions,5,&observed)==0 && !std::memcmp(&observed,&zero,sizeof(zero)));
    CHECK(artbox_signal_actions_set(actions,5,&first,&observed)==0 && observed.handler==0);
    CHECK(artbox_signal_actions_snapshot(actions,5,&observed)==0 && observed.handler==first.handler &&
        observed.mask==(UINT64_MAX&~UINT64_C(0x40100)));
    CHECK(artbox_signal_actions_set(actions,5,&first,&observed)==0); // Idempotence consumes no record.
    CHECK(artbox_signal_actions_set(actions,5,&second,&observed)==0 && observed.handler==first.handler);
    observed=unchanged;
    CHECK(artbox_signal_actions_set(actions,5,&first,&observed)==-12 && !std::memcmp(&observed,&unchanged,sizeof(observed)));
    CHECK(artbox_signal_actions_snapshot(actions,5,&observed)==0 && observed.handler==second.handler);
    CHECK(artbox_signal_actions_reset(actions,5,&observed)==0 && observed.handler==second.handler);
    CHECK(artbox_signal_actions_set(actions,5,&zero,&observed)==0 && observed.handler==0); // Default needs no record.
    for(unsigned number: {0u,65u}) {
        observed=unchanged;
        CHECK(artbox_signal_actions_snapshot(actions,number,&observed)==-22);
        CHECK(artbox_signal_actions_set(actions,number,&first,&observed)==-22);
        CHECK(!std::memcmp(&observed,&unchanged,sizeof(observed)));
    }
    for(unsigned number: {9u,19u}) {
        CHECK(artbox_signal_actions_set(actions,number,nullptr,&observed)==0 && observed.handler==0);
        CHECK(artbox_signal_actions_set(actions,number,&first,nullptr)==-22);
        CHECK(artbox_signal_actions_reset(actions,number,nullptr)==-22);
    }
    artbox_signal_actions_destroy(actions);
    actions=artbox_signal_actions_create(4096);
    CHECK(actions);
    std::atomic<bool> started{false},finished{false};
    std::thread writer([&] {
        started.store(true,std::memory_order_release);
        for(uint64_t n=1;n<=4096;++n) {
            const artbox_signal_action action{n,n^UINT64_C(0xabcdef00),n*17,0};
            CHECK(artbox_signal_actions_set(actions,11,&action,nullptr)==0);
        }
        finished.store(true,std::memory_order_release);
    });
    while(!started.load(std::memory_order_acquire)) std::this_thread::yield();
    size_t reads=0;
    do {
        CHECK(artbox_signal_actions_snapshot(actions,11,&observed)==0);
        if(observed.handler) CHECK(observed.flags==(observed.handler^UINT64_C(0xabcdef00)) &&
                                  observed.restorer==observed.handler*17 && observed.mask==0);
        if(++reads%17==0) CHECK(artbox_signal_actions_reset(actions,11,nullptr)==0);
    } while(!finished.load(std::memory_order_acquire));
    writer.join();
    CHECK(reads>0);
    artbox_signal_actions_destroy(actions);
    std::puts("Immutable action publication, concurrent snapshots/reset, capacity and output stability pass");
}
