#include "artbox/futex.h"
#include "artbox/native_atomic.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
static artbox_futex *fixture_futex;
static int fixture_errno;
extern "C" int64_t artbox_futex_check(void *);
extern "C" int *artbox_futex_errno(void) { return &fixture_errno; }
extern "C" int64_t artbox_futex_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                        uint64_t a3, uint64_t a4, uint64_t a5) {
    int64_t result = n == 98 ? artbox_futex_call(fixture_futex, a0, a1, a2, a3, a4, a5) : -38;
    if (result < 0 && result >= -4095) { fixture_errno = static_cast<int>(-result); return -1; }
    return result;
}
static bool await_waiters(artbox_futex *f, uint64_t address, int private_key, size_t count) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (artbox_futex_waiters(f, address, private_key) != count) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}
int main() {
    artbox_vm_ops memory = artbox_native_vm();
    artbox_atomic_u32_ops atomic = artbox_native_atomic_u32();
    artbox_system_ops system = artbox_native_system();
    artbox_vm *vm = artbox_vm_create(&memory, memory.page_size, 1);
    CHECK(vm);
    int64_t mapped = artbox_vm_mmap(vm, 0, memory.page_size, 3, 0x22, -1, 0);
    CHECK(mapped > 0);
    uint64_t address = static_cast<uint64_t>(mapped), timeout = address + 16;
    artbox_futex *f = artbox_futex_create(vm, &atomic, &system, 32);
    CHECK(f);
    fixture_futex = f;
    int64_t cases = artbox_futex_check(reinterpret_cast<void*>(static_cast<uintptr_t>(address)));
    if (cases != 19) std::fprintf(stderr, "Futex caller: %lld\n", static_cast<long long>(cases));
    CHECK(cases == 19);
    CHECK(artbox_vm_store_u32(vm, address, &atomic, 0) == 0);
    auto call = [&](uint64_t op, uint64_t value = 0, uint64_t time = 0, uint64_t mask = 0xffffffff) {
        return artbox_futex_call(f, address, op, value, time, 0, mask);
    };
    CHECK(call(128, 1) == -11); // Value mismatch, no wait.
    CHECK(call(128 | 256) == -38); // REALTIME requires WAIT_BITSET.
    CHECK(call(127) == -38 && call(2) == -38); // Unsupported opcodes stay unsupported.
    CHECK(call(9 | 128, 0, 0, 0) == -22);
    CHECK(call(10 | 128, 1, 0, 0) == -22);
    CHECK(artbox_futex_call(f, address + 1, 128, 0, 0, 0, 0) == -22);
    CHECK(artbox_futex_call(f, 0, 128, 0, 0, 0, 0) == -14);
    CHECK(artbox_futex_call(f, 0, 1 | 128, 1, 0, 0, 0) == 0); // A private wake does not dereference its key.
    artbox_timespec ts{0, 0};
    CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(call(128, 0, timeout) == -110);
    ts = {-1, 0}; CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(call(128, 1, timeout) == -22); // Timeout validation precedes the value check.
    ts = {0, 1000000000}; CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(call(128, 0, timeout) == -22);
    CHECK(call(128, 1, address + memory.page_size) == -14);
    ts = {0, 1000000}; CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(call(128, 0, timeout) == -110);
    ts = {0, 0}; CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(call(9 | 128, 0, timeout) == -110 && call(9 | 128 | 256, 0, timeout) == -110);
    CHECK(artbox_vm_mprotect(vm, address, memory.page_size, 1) == 0);
    CHECK(call(128, 1) == -11); // Read-only futex words remain readable.
    CHECK(artbox_vm_mprotect(vm, address, memory.page_size, 3) == 0);

    // Bitsets select waiters, and shared/private keys do not alias.
    std::atomic<int> a{99}, b{99}, shared{99};
    std::thread first([&] { a = static_cast<int>(call(9 | 128, 0, 0, 1)); });
    std::thread second([&] { b = static_cast<int>(call(9 | 128, 0, 0, 2)); });
    std::thread third([&] { shared = static_cast<int>(call(0)); });
    CHECK(await_waiters(f, address, 1, 2) && await_waiters(f, address, 0, 1));
    CHECK(artbox_futex_destroy(f) == -16);
    CHECK(call(10 | 128, 1, 0, 1) == 1);
    first.join(); CHECK(a == 0 && b == 99 && shared == 99);
    CHECK(call(1 | 128, 1) == 1);
    second.join(); CHECK(b == 0 && shared == 99);
    CHECK(artbox_vm_store_u32(vm, address, &atomic, 123) == 0);
    CHECK(artbox_futex_clear_tid(f, address) == 0);
    third.join(); CHECK(shared == 0);
    uint32_t tid = 99;
    CHECK(artbox_vm_load_u32(vm, address, &atomic, &tid) == 0 && tid == 0);

    // Linux's original FUTEX_WAKE wakes one even for a zero/negative count.
    // Preserve that observed ABI quirk instead of asserting the documented maximum.
    for (uint64_t count : {UINT64_C(0), UINT64_MAX}) {
        std::thread waiter([&] { a = static_cast<int>(call(128)); });
        CHECK(await_waiters(f, address, 1, 1));
        CHECK(call(1 | 128, count) == 1); waiter.join(); CHECK(a == 0);
    }
    // Race the value transition against queue insertion. No wake can be lost.
    for (unsigned i = 0; i < 512; ++i) {
        CHECK(artbox_vm_store_u32(vm, address, &atomic, 0) == 0);
        std::thread waiter([&] { a = static_cast<int>(call(128)); });
        CHECK(artbox_vm_store_u32(vm, address, &atomic, 1) == 0);
        int64_t woken = call(1 | 128, 1);
        CHECK(woken == 0 || woken == 1);
        waiter.join(); CHECK(a == 0 || a == -11);
    }
#if defined(__linux__)
    auto linux_futex = [&](uint64_t pointer, unsigned op, uint32_t value, uint64_t time, uint32_t mask) {
        long r = syscall(SYS_futex, static_cast<uintptr_t>(pointer), op, value, static_cast<uintptr_t>(time), 0, mask);
        return r < 0 ? -errno : static_cast<int>(r);
    };
    CHECK(linux_futex(address, 128, 0, 0, 0) == -11);
    CHECK(linux_futex(address, 128 | 256, 1, 0, 0) == -38);
    CHECK(linux_futex(address + 1, 128, 1, 0, 0) == -22);
    CHECK(linux_futex(0, 128, 0, 0, 0) == -14 && linux_futex(0, 129, 1, 0, 0) == 0);
    CHECK(linux_futex(address, 137, 1, 0, 0) == -22);
    ts = {0, 0}; CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(linux_futex(address, 128, 1, timeout, 0) == -110);
    ts = {-1, 0}; CHECK(artbox_vm_write(vm, timeout, &ts, sizeof(ts)) == 0);
    CHECK(linux_futex(address, 128, 0, timeout, 0) == -22);
    for (uint32_t count : {0u, 0xffffffffu}) {
        std::thread waiter([&] { a = linux_futex(address, 128, 1, 0, 0); });
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        int woken;
        do {
            woken = linux_futex(address, 129, count, 0, 0);
            CHECK(std::chrono::steady_clock::now() < end);
            std::this_thread::yield();
        } while (!woken);
        CHECK(woken == 1); waiter.join(); CHECK(a == 0);
    }
#endif
    CHECK(artbox_futex_clear_tid(f, 0) == 0);
    CHECK(artbox_futex_destroy(f) == 0 && artbox_vm_destroy(vm) == 0);
    std::puts("Futex contract: timed waits, bitsets, distinct keys, clear-TID and 512 enqueue/wake races passed");
    return 0;
}
