// Original user-mode wait queue, MIT. Linux behavior is checked by paired tests.
#include "artbox/futex.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

struct Waiter {
    uint64_t address;
    uint32_t mask;
    bool private_key, woken = false;
    std::condition_variable changed;
    Waiter(uint64_t a, uint32_t m, bool p) : address(a), mask(m), private_key(p) {}
};
struct artbox_futex {
    artbox_vm *vm;
    artbox_atomic_u32_ops atomic;
    artbox_system_ops system;
    size_t limit;
    std::mutex lock;
    std::vector<Waiter*> active;

    unsigned wake(uint64_t address, bool private_key, uint32_t mask, int32_t count) {
        // The original Linux futex syscall has this historical behavior for
        // nonpositive wake counts. The newer futex2 strict interface differs.
        unsigned maximum = count > 0 ? static_cast<unsigned>(count) : 1u, done = 0;
        for (Waiter *w : active) {
            if (!w->woken && w->address == address && w->private_key == private_key && (w->mask & mask)) {
                w->woken = true;
                w->changed.notify_one();
                if (++done == maximum) break;
            }
        }
        return done;
    }
};
extern "C" artbox_futex *artbox_futex_create(artbox_vm *vm, const artbox_atomic_u32_ops *atomic,
                                             const artbox_system_ops *system, size_t limit) {
    if (!vm || !atomic || !atomic->load_acquire || !atomic->store_release || !system || !system->clock ||
        !limit || limit > 65536) return nullptr;
    artbox_futex *f = new (std::nothrow) artbox_futex;
    if (!f) return nullptr;
    f->vm = vm; f->atomic = *atomic; f->system = *system; f->limit = limit;
    try { f->active.reserve(limit); }
    catch (const std::exception&) { delete f; return nullptr; }
    return f;
}
extern "C" int artbox_futex_destroy(artbox_futex *f) {
    if (!f) return -22;
    { std::lock_guard<std::mutex> guard(f->lock); if (!f->active.empty()) return -16; }
    delete f;
    return 0;
}
static int clock_value(artbox_futex *f, unsigned id, artbox_timespec *value) {
    int error = f->system.clock(id, value);
    if (error) return error < 0 ? error : -5;
    return value->seconds < 0 || value->nanoseconds < 0 || value->nanoseconds >= 1000000000 ? -5 : 0;
}
static uint64_t u64(const unsigned char *p) {
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= static_cast<uint64_t>(p[i]) << (8 * i);
    return result;
}
static int timeout_value(artbox_futex *f, uint64_t pointer, bool relative, artbox_timespec *deadline) {
    unsigned char bytes[16];
    int error = artbox_vm_read(f->vm, pointer, bytes, sizeof(bytes));
    if (error) return error;
    deadline->seconds = static_cast<int64_t>(u64(bytes));
    deadline->nanoseconds = static_cast<int64_t>(u64(bytes + 8));
    if (deadline->seconds < 0 || deadline->nanoseconds < 0 || deadline->nanoseconds >= 1000000000) return -22;
    if (relative) {
        artbox_timespec now;
        if ((error = clock_value(f, 1, &now))) return error;
        if (deadline->seconds > INT64_MAX - now.seconds) { *deadline = {INT64_MAX, 999999999}; return 0; }
        deadline->seconds += now.seconds;
        deadline->nanoseconds += now.nanoseconds;
        if (deadline->nanoseconds >= 1000000000) {
            if (deadline->seconds == INT64_MAX) { *deadline = {INT64_MAX, 999999999}; return 0; }
            ++deadline->seconds; deadline->nanoseconds -= 1000000000;
        }
    }
    return 0;
}
extern "C" int64_t artbox_futex_call(artbox_futex *f, uint64_t address, uint64_t operation,
                                     uint64_t value, uint64_t timeout, uint64_t address2, uint64_t bitset) {
    (void)address2;
    if (!f) return -22;
    uint32_t op = static_cast<uint32_t>(operation), command = op & ~UINT32_C(384);
    bool private_key = (op & 128) != 0, realtime = (op & 256) != 0;
    bool waiting = command == 0 || command == 9;
    artbox_timespec deadline{};
    // Linux validates a supplied timeout before dispatching its wait opcode.
    if (waiting && timeout) {
        int error = timeout_value(f, timeout, command == 0, &deadline);
        if (error) return error;
    }
    if ((realtime && command != 9) || (!waiting && command != 1 && command != 10)) return -38;
    uint32_t mask = command == 0 || command == 1 ? UINT32_MAX : static_cast<uint32_t>(bitset);
    if (!mask || (address & 3)) return -22;
    if (address > static_cast<uint64_t>(INT64_MAX) - 4) return -14;
    try {
        std::unique_lock<std::mutex> guard(f->lock);
        if (!waiting) {
            // Private wake keys need not currently be mapped. Shared-file
            // aliases are unsupported; shared same-address keys must be live.
            if (!private_key && !artbox_vm_access(f->vm, address, 4, 1)) return -14;
            return f->wake(address, private_key, mask, static_cast<int32_t>(value));
        }
        uint32_t actual;
        int error = artbox_vm_load_u32(f->vm, address, &f->atomic, &actual);
        if (error) return error;
        if (actual != static_cast<uint32_t>(value)) return -11;
        if (f->active.size() == f->limit) return -12;
        Waiter waiter(address, mask, private_key);
        // The same lock serializes value check/enqueue against every wake.
        // The VM lock is released before blocking so mappings remain usable.
        f->active.push_back(&waiter);
        int result = 0;
        try {
            while (!waiter.woken) {
                if (!timeout) { waiter.changed.wait(guard); continue; }
                artbox_timespec now;
                if ((result = clock_value(f, realtime ? 0 : 1, &now))) break;
                if (now.seconds > deadline.seconds ||
                    (now.seconds == deadline.seconds && now.nanoseconds >= deadline.nanoseconds)) { result = -110; break; }
                int64_t seconds = deadline.seconds - now.seconds;
                int64_t ns = seconds > 1 ? 100000000 : seconds * INT64_C(1000000000) + deadline.nanoseconds - now.nanoseconds;
                // Recheck the actual guest clock, including realtime changes.
                // No assumption about std::chrono::steady_clock's epoch is used.
                waiter.changed.wait_for(guard, std::chrono::nanoseconds(std::min(ns, INT64_C(100000000))));
            }
        } catch (const std::exception&) { result = -5; }
        auto position = std::find(f->active.begin(), f->active.end(), &waiter);
        f->active.erase(position);
        return result;
    } catch (const std::exception&) { return -12; }
}
extern "C" int artbox_futex_clear_tid(artbox_futex *f, uint64_t address) {
    if (!f) return -22;
    if (!address) return 0;
    std::lock_guard<std::mutex> guard(f->lock);
    int error = artbox_vm_store_u32(f->vm, address, &f->atomic, 0);
    if (error) return error;
    (void)f->wake(address, false, UINT32_MAX, 1);
    return 0;
}
extern "C" size_t artbox_futex_waiters(artbox_futex *f, uint64_t address, int private_key) {
    if (!f) return 0;
    std::lock_guard<std::mutex> guard(f->lock);
    size_t count = 0;
    for (const Waiter *w : f->active)
        if (!w->woken && w->address == address && w->private_key == (private_key != 0)) ++count;
    return count;
}
