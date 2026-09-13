#include "artbox/native_vm.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <cerrno>
#include <sys/mman.h>
#endif

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

struct Calls {
    artbox_vm *vm;
    bool linux_oracle;
    int64_t map(uint64_t address, size_t size, unsigned prot, unsigned flags = 0x22) {
#if defined(__linux__)
        if (linux_oracle) {
            void *p = mmap(reinterpret_cast<void *>(address), size, static_cast<int>(prot), static_cast<int>(flags), -1, 0);
            return p == MAP_FAILED ? -errno : static_cast<int64_t>(reinterpret_cast<uintptr_t>(p));
        }
#endif
        return artbox_vm_mmap(vm, address, size, prot, flags, -1, 0);
    }
    int protect(uint64_t address, size_t size, unsigned prot) {
#if defined(__linux__)
        if (linux_oracle) return mprotect(reinterpret_cast<void *>(address), size, static_cast<int>(prot)) ? -errno : 0;
#endif
        return artbox_vm_mprotect(vm, address, size, prot);
    }
    int unmap(uint64_t address, size_t size) {
#if defined(__linux__)
        if (linux_oracle) return munmap(reinterpret_cast<void *>(address), size) ? -errno : 0;
#endif
        return artbox_vm_munmap(vm, address, size);
    }
    int discard(uint64_t address, size_t size) {
#if defined(__linux__)
        if (linux_oracle) return madvise(reinterpret_cast<void *>(address), size, MADV_DONTNEED) ? -errno : 0;
#endif
        return artbox_vm_madvise(vm, address, size, 4);
    }
};

static Calls fixture_calls;
static int fixture_errno;
extern "C" int64_t artbox_vm_check(size_t page);
extern "C" int *artbox_stub___errno(void) { return &fixture_errno; }
extern "C" int64_t artbox_stub_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                      uint64_t a3, uint64_t a4, uint64_t a5) {
    int64_t result = -38;
    (void)a4; (void)a5;
    switch (n) {
        case 222: result = fixture_calls.map(a0, static_cast<size_t>(a1), static_cast<unsigned>(a2), static_cast<unsigned>(a3)); break;
        case 226: result = fixture_calls.protect(a0, static_cast<size_t>(a1), static_cast<unsigned>(a2)); break;
        case 215: result = fixture_calls.unmap(a0, static_cast<size_t>(a1)); break;
        case 233:
#if defined(__linux__)
            if (fixture_calls.linux_oracle) {
                result = madvise(reinterpret_cast<void *>(a0), static_cast<size_t>(a1), static_cast<int>(a2)) ? -errno : 0;
                break;
            }
#endif
            result = artbox_vm_madvise(fixture_calls.vm, a0, a1, static_cast<int>(a2)); break;
    }
    if (result < 0 && result >= -4095) { fixture_errno = static_cast<int>(-result); return -1; }
    return result;
}

static artbox_vm_ops backing;
static unsigned fail_protect, fail_release, released;
static int injected_protect(void *p, size_t n, unsigned prot) {
    int result = backing.protect(p, n, prot);
    if (fail_protect) { --fail_protect; return -12; } // Error after a native mutation.
    return result;
}
static int injected_release(void *p, size_t n) {
    if (fail_release) { --fail_release; return -12; }
    int result = backing.release(p, n);
    if (!result) ++released;
    return result;
}

static int failures_and_limits(artbox_vm_ops ops) {
    backing = ops;
    ops.protect = injected_protect;
    ops.release = injected_release;
    artbox_vm *vm = artbox_vm_create(&ops, ops.page_size * 2, 1);
    CHECK(vm != nullptr);
    fail_protect = 1;
    CHECK(artbox_vm_mmap(vm, 0, ops.page_size, 3, 0x22, -1, 0) == -12);
    CHECK(released == 1 && artbox_vm_reserved_bytes(vm) == 0);
    int64_t address = artbox_vm_mmap(vm, 0, ops.page_size * 2, 3, 0x22, -1, 0);
    CHECK(address > 0);
    CHECK(artbox_vm_mmap(vm, 0, ops.page_size, 3, 0x22, -1, 0) == -12);
    CHECK(artbox_vm_munmap(vm, static_cast<uint64_t>(address), ops.page_size * 2) == 0);
    address = artbox_vm_mmap(vm, 0, ops.page_size, 3, 0x22, -1, 0);
    CHECK(address > 0); // Releasing the last live page recovers the reservation quota.
    fail_protect = 1;
    CHECK(artbox_vm_mprotect(vm, static_cast<uint64_t>(address), ops.page_size, 1) == -12);
    CHECK(!artbox_vm_access(vm, static_cast<uint64_t>(address), 1, 1));
    CHECK(artbox_vm_munmap(vm, static_cast<uint64_t>(address), ops.page_size) == -5);
    CHECK(artbox_vm_mmap(vm, 0, ops.page_size, 3, 0x22, -1, 0) == -5);
    CHECK(artbox_vm_destroy(vm) == 0 && released == 3);
    vm = artbox_vm_create(&ops, ops.page_size * 2, 1);
    CHECK(vm != nullptr);
    fail_protect = fail_release = 1;
    CHECK(artbox_vm_mmap(vm, 0, ops.page_size, 3, 0x22, -1, 0) == -12);
    CHECK(artbox_vm_reserved_bytes(vm) == ops.page_size); // Failed rollback remains tracked.
    CHECK(artbox_vm_mmap(vm, 0, ops.page_size, 3, 0x22, -1, 0) == -5);
    CHECK(artbox_vm_destroy(vm) == 0 && released == 4);
    return 0;
}

/* The same operations and byte expectations run against the real Linux ABI. */
static int semantics(Calls calls, size_t page) {
    CHECK(calls.map(0, 0, 3) == -22);
    int64_t address = calls.map(0, page * 3 - 1, 3);
    CHECK(address > 0 && static_cast<uint64_t>(address) % page == 0);
    uint64_t base = static_cast<uint64_t>(address);
    unsigned char *bytes = reinterpret_cast<unsigned char *>(base);
    for (size_t i = 0; i < page * 3; ++i) CHECK(bytes[i] == 0);
    std::memset(bytes, 0x11, page);
    std::memset(bytes + page, 0x22, page);
    std::memset(bytes + 2 * page, 0x33, page);
    CHECK(calls.protect(base + 1, page, 1) == -22);
    CHECK(calls.protect(base + page, page - 1, 1) == 0);
    CHECK(bytes[page] == 0x22 && bytes[page * 2 - 1] == 0x22);
    CHECK(calls.protect(base, page, 0) == 0);
    CHECK(calls.protect(base, page, 3) == 0 && bytes[0] == 0x11);
    CHECK(calls.unmap(base + page, page) == 0);
    CHECK(calls.protect(base + page, page, 1) == -12);
    CHECK(bytes[0] == 0x11 && bytes[2 * page] == 0x33);
    CHECK(calls.map(base + page, page, 3, 0x4032) == address + static_cast<int64_t>(page));
    for (size_t i = page; i < page * 2; ++i) CHECK(bytes[i] == 0);
    std::memset(bytes + page, 0x44, page);
    CHECK(calls.protect(base + page, page, 1) == 0);
    CHECK(calls.discard(base + page, page) == 0);
    for (size_t i = page; i < page * 2; ++i) CHECK(bytes[i] == 0);
    CHECK(bytes[0] == 0x11 && bytes[2 * page] == 0x33);
    CHECK(calls.unmap(base + page, page) == 0);
    CHECK(calls.unmap(base, page * 3) == 0); // Includes the already-unmapped hole.
    CHECK(calls.unmap(base, page * 3) == 0);
    CHECK(calls.protect(base, 0, 1) == 0);
    CHECK(calls.unmap(base, 0) == -22);
    address = calls.map(0, page * 8, 0, 0x4022);
    CHECK(address > 0);
    base = static_cast<uint64_t>(address);
    CHECK(calls.map(base + page * 2, page * 2, 3, 0x32) == address + static_cast<int64_t>(page * 2));
    bytes = reinterpret_cast<unsigned char *>(base + page * 2);
    for (size_t i = 0; i < page * 2; ++i) CHECK(bytes[i] == 0);
    std::memset(bytes, 0x55, page * 2);
    CHECK(calls.map(base + page * 2, page, 3, 0x32) == address + static_cast<int64_t>(page * 2));
    CHECK(bytes[0] == 0 && bytes[page] == 0x55);
    CHECK(calls.unmap(base, page * 2) == 0);
    CHECK(calls.unmap(base + page * 4, page * 4) == 0);
    CHECK(bytes[page] == 0x55);
    CHECK(calls.unmap(base + page * 2, page * 2) == 0);
    return 0;
}

int main() {
    artbox_vm_ops ops = artbox_native_vm();
    size_t page = ops.page_size;
    artbox_vm *vm = artbox_vm_create(&ops, UINT64_C(16) << 30, 256);
    CHECK(vm != nullptr);
    CHECK(semantics({vm, false}, page) == 0);
    fixture_calls = {vm, false};
    int64_t fixture_result = artbox_vm_check(page);
    if (fixture_result != 35) std::fprintf(stderr, "VM fixture: %lld\n", static_cast<long long>(fixture_result));
    CHECK(fixture_result == 35);
    CHECK(artbox_vm_reserved_bytes(vm) == 0);
#if defined(__linux__)
    CHECK(semantics({nullptr, true}, page) == 0);
    fixture_calls = {nullptr, true};
    CHECK(artbox_vm_check(page) == 35);
#endif
    CHECK(artbox_vm_syscall(vm, 9999, 0, 0, 0, 0, 0, 0) == -38);
    // Deliberate host-ownership and no-executable-data policies are separate
    // from the paired Linux cases above.
    CHECK(artbox_vm_mmap(vm, 0, page, 7, 0x22, -1, 0) == -1);
    CHECK(artbox_vm_mmap(vm, 0, page, 3, 0x21, -1, 0) == -95);
    CHECK(artbox_vm_mmap(vm, 0, page, 3, 0x22, -1, 1) == -22);
    CHECK(artbox_vm_mmap(vm, 0, UINT64_MAX, 3, 0x22, -1, 0) == -22);
    CHECK(artbox_vm_mmap(vm, page, page, 3, 0x32, -1, 0) == -95);
    CHECK(artbox_vm_mmap(vm, 0, UINT64_C(17) << 30, 0, 0x4022, -1, 0) == -12);
    int64_t mapped = artbox_vm_mmap(vm, 0, page * 3, 3, 0x22, 123, page);
    CHECK(mapped > 0); // Anonymous mappings ignore fd and aligned offset.
    uint64_t base = static_cast<uint64_t>(mapped);
    CHECK(artbox_vm_access(vm, base, page * 3, 3));
    CHECK(artbox_vm_mprotect(vm, base + page, page, 1) == 0);
    CHECK(artbox_vm_access(vm, base, page * 3, 1));
    CHECK(!artbox_vm_access(vm, base, page * 3, 2));
    CHECK(artbox_vm_mprotect(vm, base, page, 4) == -1);
    CHECK(artbox_vm_munmap(vm, base + page, page) == 0);
    CHECK(!artbox_vm_access(vm, base, page * 3, 1));
    CHECK(artbox_vm_madvise(vm, base, page * 3, 4) == -12);
    CHECK(artbox_vm_munmap(vm, base, page * 3) == 0);

    void *borrowed = nullptr;
    CHECK(ops.reserve(page * 2, &borrowed) == 0 && ops.protect(borrowed, page * 2, 3) == 0);
    base = reinterpret_cast<uintptr_t>(borrowed);
    CHECK(artbox_vm_register_data(vm, borrowed, page * 2, 3) == 0);
    CHECK(artbox_vm_register_data(vm, borrowed, page, 3) == -17);
    CHECK(artbox_vm_mprotect(vm, base, page, 1) == 0);
    CHECK(artbox_vm_mmap(vm, base, page, 3, 0x32, -1, 0) == -1);
    CHECK(artbox_vm_munmap(vm, base, page) == -1);
    CHECK(artbox_vm_madvise(vm, base, page, 4) == -1);
    CHECK(artbox_vm_mprotect(vm, base, page, 3) == 0);
    CHECK(artbox_vm_reserved_bytes(vm) == 0);

    std::atomic<unsigned> failures{0};
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back([&] {
        for (unsigned n = 0; n < 128; ++n) {
            int64_t p = artbox_vm_mmap(vm, 0, page * 4, 3, 0x22, -1, 0);
            if (p <= 0) { ++failures; continue; }
            uint64_t v = static_cast<uint64_t>(p);
            std::memset(reinterpret_cast<void *>(v), 0x66, page * 4);
            if (artbox_vm_mprotect(vm, v + page, page, 1) ||
                artbox_vm_madvise(vm, v + page, page, 4) ||
                *reinterpret_cast<unsigned char *>(v + page) != 0 ||
                artbox_vm_munmap(vm, v + page * 2, page) || artbox_vm_munmap(vm, v, page * 4)) ++failures;
        }
    });
    for (auto &thread : threads) thread.join();
    CHECK(failures == 0 && artbox_vm_reserved_bytes(vm) == 0);
    CHECK(artbox_vm_destroy(vm) == 0);
    std::memset(borrowed, 0x77, page * 2); // VM destruction did not free borrowed storage.
    CHECK(ops.release(borrowed, page * 2) == 0);
    CHECK(failures_and_limits(ops) == 0);
    std::printf("VM: %u checks and 1024 concurrent mapping lifecycles passed\n", checks);
    return 0;
}
