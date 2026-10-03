// Original immutable Linux signal-action storage. SPDX-License-Identifier: MIT
#include "artbox/signal_actions.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>

struct artbox_signal_actions {
    artbox_signal_action empty{};
    artbox_signal_action *records;
    size_t capacity,used=0;
    std::atomic<const artbox_signal_action*> current[65];
    std::mutex writer;
};
static bool valid(unsigned number) { return number>=1 && number<=64; }
static bool changeable(unsigned number) { return valid(number) && number!=9 && number!=19; }
extern "C" artbox_signal_actions *artbox_signal_actions_create(size_t capacity) {
    if(!capacity || capacity>65536) return nullptr;
    auto *actions=new(std::nothrow) artbox_signal_actions;
    if(!actions) return nullptr;
    actions->capacity=capacity;
    actions->records=new(std::nothrow) artbox_signal_action[capacity];
    if(!actions->records) { delete actions; return nullptr; }
    for(auto &entry:actions->current) entry.store(&actions->empty,std::memory_order_relaxed);
    if(!actions->current[0].is_lock_free()) {
        delete[] actions->records; delete actions; return nullptr;
    }
    return actions;
}
extern "C" void artbox_signal_actions_destroy(artbox_signal_actions *actions) {
    if(actions) { delete[] actions->records; delete actions; }
}
extern "C" int artbox_signal_actions_snapshot(const artbox_signal_actions *actions,unsigned number,
    artbox_signal_action *output) {
    if(!actions || !valid(number) || !output) return -22;
    *output=*actions->current[number].load(std::memory_order_acquire);
    return 0;
}
extern "C" int artbox_signal_actions_reset(artbox_signal_actions *actions,unsigned number,
    artbox_signal_action *previous) {
    if(!actions || !changeable(number)) return -22;
    const auto *old=actions->current[number].exchange(&actions->empty,std::memory_order_acq_rel);
    if(previous) *previous=*old;
    return 0;
}
extern "C" int artbox_signal_actions_set(artbox_signal_actions *actions,unsigned number,
    const artbox_signal_action *input,artbox_signal_action *previous) {
    if(!actions || !valid(number) || (input && !changeable(number))) return -22;
    if(!input) {
        if(previous) *previous=*actions->current[number].load(std::memory_order_acquire);
        return 0;
    }
    artbox_signal_action requested=*input;
    requested.mask&=~UINT64_C(0x40100);
    if(!std::memcmp(&requested,&actions->empty,sizeof(requested)))
        return artbox_signal_actions_reset(actions,number,previous);
    std::lock_guard<std::mutex> guard(actions->writer);
    const auto *old=actions->current[number].load(std::memory_order_acquire);
    if(!std::memcmp(old,&requested,sizeof(requested))) {
        if(previous) *previous=*old;
        return 0;
    }
    if(actions->used==actions->capacity) return -12;
    auto *record=&actions->records[actions->used++];
    *record=requested;
    // Return the actual preceding action even if a handler reset interleaved
    // with preparation. Published records are never overwritten or reclaimed.
    old=actions->current[number].exchange(record,std::memory_order_acq_rel);
    if(previous) *previous=*old;
    return 0;
}
