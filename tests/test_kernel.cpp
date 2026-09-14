#include "artbox/kernel.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#if defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
static int64_t call(artbox_kernel_thread &t, uint64_t n, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0) {
    return artbox_kernel_call(&t, n, a0, a1, a2, 0, 0, 0);
}
static artbox_kernel_thread *fixture_thread;
static int fixture_errno;
extern "C" int64_t artbox_system_check(size_t, uint64_t, uint64_t);
extern "C" int *artbox_stub___errno(void) { return &fixture_errno; }
extern "C" int64_t artbox_stub_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                      uint64_t a3, uint64_t a4, uint64_t a5) {
    int64_t result = artbox_kernel_call(fixture_thread, n, a0, a1, a2, a3, a4, a5);
    if (result < 0 && result >= -4095) { fixture_errno = static_cast<int>(-result); return -1; }
    return result;
}
static int bad_random(void *, size_t) { return -5; }
static int bad_clock(unsigned, artbox_timespec *) { return -5; }
static bool before(const artbox_timespec &a, const artbox_timespec &b) {
    return a.seconds < b.seconds || (a.seconds == b.seconds && a.nanoseconds <= b.nanoseconds);
}
int main() {
    artbox_vm_ops memory = artbox_native_vm();
    artbox_vm *vm = artbox_vm_create(&memory, memory.page_size * 4, 4);
    artbox_system_ops system = artbox_native_system();
    artbox_kernel_thread a, b;
    CHECK(vm && artbox_kernel_thread_init(&a, vm, &system, 100, 100) == 0);
    CHECK(artbox_kernel_thread_init(&b, vm, &system, 100, 101) == 0);
    fixture_thread = &a;
    int64_t fixture = artbox_system_check(memory.page_size, 100, 100);
    if (fixture != 53) std::fprintf(stderr, "Startup fixture: %lld\n", static_cast<long long>(fixture));
    CHECK(fixture == 53);
    CHECK(call(a, 172) == call(b, 172) && call(a, 178) == 100 && call(b, 178) == 101);
    CHECK(call(a, 96, UINT64_MAX) == 100 && a.clear_tid_address == UINT64_MAX);
    CHECK(call(a, 96, 0) == 100 && a.clear_tid_address == 0 && b.clear_tid_address == 0);
    CHECK(call(a, 9999) == -38);
    int64_t mapped = artbox_kernel_call(&a, 222, 0, memory.page_size * 2, 3, 0x22, UINT64_MAX, 0);
    CHECK(mapped > 0);
    uint64_t address = static_cast<uint64_t>(mapped);
    auto *bytes = reinterpret_cast<unsigned char *>(address);
    std::memset(bytes, 0xa5, memory.page_size * 2);
    CHECK(call(a, 278, 0, 0, 0) == 0);
    CHECK(call(a, 278, address, 16, 8) == -22 && bytes[0] == 0xa5);
    CHECK(call(a, 278, address, 16, 6) == -22 && bytes[0] == 0xa5);
    for (unsigned flags : {0u, 1u, 2u, 3u, 4u, 5u}) {
        CHECK(call(a, 278, address + 1, 512, flags) == 512);
        CHECK(bytes[0] == 0xa5 && bytes[513] == 0xa5);
    }
    // No statistical pass/fail assertion on random bytes: the test checks the
    // syscall contract and canaries, while the backend is the OS CSPRNG.
    CHECK(call(a, 278, 0, 16) == -14);
    CHECK(call(a, 226, address, memory.page_size, 1) == 0);
    CHECK(call(a, 278, address, 16) == -14);
    CHECK(call(a, 113, 0, address) == -14);
    CHECK(call(a, 226, address, memory.page_size, 3) == 0);
    CHECK(call(a, 226, address + memory.page_size, memory.page_size, 0) == 0);
    CHECK(call(a, 278, address, memory.page_size * 2) == static_cast<int64_t>(memory.page_size));
#if defined(__linux__)
    CHECK(getrandom(bytes, memory.page_size * 2, 0) == static_cast<ssize_t>(memory.page_size));
#endif
    CHECK(call(a, 226, address + memory.page_size, memory.page_size, 3) == 0);
    CHECK(call(a, 113, 99, address) == -22);
    CHECK(call(a, 113, 0, 0) == -14);
    for (unsigned id : {0u, 1u, 5u, 6u}) {
        artbox_timespec early{}, guest{}, late{};
        unsigned native_id = id >= 5 ? id - 5 : id;
        CHECK(system.clock(native_id, &early) == 0);
        CHECK(call(a, 113, id, address + 1) == 0); // Linux permits an unaligned output structure.
        CHECK(artbox_vm_read(vm, address + 1, &guest, sizeof(guest)) == 0);
        CHECK(system.clock(native_id, &late) == 0);
        CHECK(guest.nanoseconds >= 0 && guest.nanoseconds < 1000000000 && before(early, guest) && before(guest, late));
#if defined(__linux__)
        struct timespec coarse;
        CHECK(clock_gettime(static_cast<clockid_t>(id), &coarse) == 0);
        CHECK(coarse.tv_nsec >= 0 && coarse.tv_nsec < 1000000000);
        // A coarse kernel snapshot can trail the precise clock by a tick.
        CHECK(coarse.tv_sec >= guest.seconds - 1 && coarse.tv_sec <= guest.seconds + 1);
#endif
    }
    a.system.random = bad_random;
    CHECK(call(a, 278, address, 16) == -5);
    a.system.clock = bad_clock;
    CHECK(call(a, 113, 0, address) == -5);
#if defined(__linux__)
    CHECK(getrandom(nullptr, 0, 0) == 0);
    CHECK(getrandom(bytes, 16, 8) == -1 && errno == EINVAL);
    CHECK(getrandom(bytes, 16, 6) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_getrandom, 0, 16, 0) == -1 && errno == EFAULT);
    for (unsigned flags : {0u, 1u, 2u, 3u, 4u, 5u}) CHECK(getrandom(bytes + 1, 16, flags) == 16);
    struct timespec ts;
    CHECK(clock_gettime(static_cast<clockid_t>(99), &ts) == -1 && errno == EINVAL);
#endif
    CHECK(artbox_kernel_call(&b, 215, address, memory.page_size * 2, 0, 0, 0, 0) == 0);
    CHECK(artbox_vm_reserved_bytes(vm) == 0 && artbox_vm_destroy(vm) == 0);
    std::puts("Kernel startup services: virtual IDs, CSPRNG, clock layout and errors passed");
    return 0;
}
