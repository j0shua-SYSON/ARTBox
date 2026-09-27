// Original coherent mask/pending storage. SPDX-License-Identifier: MIT
#ifndef ARTBOX_SIGNAL_STATE_INTERNAL_H
#define ARTBOX_SIGNAL_STATE_INTERNAL_H
#include <atomic>
#include <cstdint>
#include <mutex>
struct SignalBits { uint64_t mask,pending; };
class SignalState {
#if defined(__aarch64__) && defined(__clang__)
    // The compiler emits an exclusive-pair retry loop. Build this translation
    // unit without outlined atomics; no libatomic dispatch is allowed here.
    alignas(16) mutable SignalBits bits_{};
    static_assert(sizeof(SignalBits)==16 && __atomic_always_lock_free(16,nullptr),
        "Signal mask transitions require lock-free 128-bit operations");
public:
    static bool handler_safe() { return true; }
    bool snapshot_lock_free() const { return true; }
    void initialize(uint64_t mask) { bits_={mask,0}; } // Before publication only.
    SignalBits load() const {
        SignalBits value;
        __atomic_load(&bits_,&value,__ATOMIC_ACQUIRE);
        return value;
    }
    uint64_t mask() const { return load().mask; }
    bool compare_exchange(SignalBits &expected,SignalBits desired) {
        return __atomic_compare_exchange(&bits_,&expected,&desired,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    }
#else
    // Ordinary callers on other hosts retain the same transition semantics.
    // Only the separately published query is signal-safe on these targets.
    SignalBits bits_{};
    mutable std::mutex lock_;
    std::atomic<uint64_t> mask_{0};
public:
    static bool handler_safe() { return false; }
    bool snapshot_lock_free() const { return mask_.is_lock_free(); }
    void initialize(uint64_t mask) { bits_={mask,0}; mask_.store(mask,std::memory_order_relaxed); }
    SignalBits load() const { std::lock_guard<std::mutex> guard(lock_); return bits_; }
    uint64_t mask() const { return mask_.load(std::memory_order_acquire); }
    bool compare_exchange(SignalBits &expected,SignalBits desired) {
        std::lock_guard<std::mutex> guard(lock_);
        if(bits_.mask!=expected.mask || bits_.pending!=expected.pending) { expected=bits_; return false; }
        bits_=desired; mask_.store(desired.mask,std::memory_order_release);
        return true;
    }
#endif
};
#endif
