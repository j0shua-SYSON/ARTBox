/* Startup syscall contract through the real NDK-built Bionic errno tail. */
#include <stddef.h>
#include <stdint.h>
extern int64_t artbox_stub_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_stub___errno(void);
#define CALL(n, a, b, c) artbox_stub_syscall(n, a, b, c, 0, 0, 0)
#define CHECK(x) do { if (!(x)) return -__LINE__; ++cases; } while (0)
static uint64_t word(const volatile unsigned char *p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}
static void store(volatile unsigned char *p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(value >> (8 * i));
}
#define MASK(h, n, o, size) artbox_stub_syscall(135, h, n, o, size, 0, 0)
int64_t artbox_system_check(size_t page, uint64_t pid, uint64_t tid) {
    int64_t cases = 0;
    CHECK(CALL(172, 0, 0, 0) == (int64_t)pid);
    CHECK(CALL(178, 0, 0, 0) == (int64_t)tid);
    // The Linux runner is a disposable single-thread process. Clearing its exit
    // TID registration cannot affect another runtime or a pthread_join caller.
    CHECK(CALL(96, 0, 0, 0) == (int64_t)tid);
    int64_t mapped = artbox_stub_syscall(222, 0, page * 2, 3, 0x22, UINT64_MAX, 0);
    CHECK(mapped > 0);
    uint64_t address = (uint64_t)mapped;
    volatile unsigned char *bytes = (volatile unsigned char *)(uintptr_t)address;
    for (size_t i = 0; i < page * 2; ++i) bytes[i] = 0xa5;
    CHECK(CALL(278, 0, 0, 0) == 0);
    CHECK(CALL(278, address, 16, 8) == -1 && *artbox_stub___errno() == 22 && bytes[0] == 0xa5);
    CHECK(CALL(278, address, 16, 6) == -1 && *artbox_stub___errno() == 22 && bytes[0] == 0xa5);
    for (unsigned flags = 0; flags < 6; ++flags) {
        CHECK(CALL(278, address + 1, 512, flags) == 512);
        CHECK(bytes[0] == 0xa5 && bytes[513] == 0xa5);
    }
    CHECK(CALL(278, 0, 16, 0) == -1 && *artbox_stub___errno() == 14);
    CHECK(CALL(226, address, page, 1) == 0);
    CHECK(CALL(278, address, 16, 0) == -1 && *artbox_stub___errno() == 14);
    CHECK(CALL(113, 0, address, 0) == -1 && *artbox_stub___errno() == 14);
    CHECK(CALL(226, address, page, 3) == 0);
    CHECK(CALL(113, 99, address, 0) == -1 && *artbox_stub___errno() == 22);
    CHECK(CALL(113, 0, 0, 0) == -1 && *artbox_stub___errno() == 14);
    for (unsigned id = 0; id < 2; ++id) {
        CHECK(CALL(113, id, address + 1, 0) == 0);
        uint64_t seconds = word(bytes + 1), nanos = word(bytes + 9);
        CHECK(nanos < 1000000000 && seconds < INT64_MAX);
        CHECK(CALL(113, id, address + 33, 0) == 0);
        uint64_t later_seconds = word(bytes + 33), later_nanos = word(bytes + 41);
        CHECK(later_nanos < 1000000000 && later_seconds < INT64_MAX);
        if (id == 1) CHECK(seconds < later_seconds || (seconds == later_seconds && nanos <= later_nanos));
    }
    // Signal-mask storage and error ordering, independent of host sigset_t.
    // Save and restore the disposable Linux oracle's real mask.
    uint64_t saved = address + 128, input = address + 1, output = address + 17;
    CHECK(MASK(999, 0, saved, 8) == 0); // how is ignored without a new mask.
    CHECK(MASK(2, 0, 0, 0) == -1 && *artbox_stub___errno() == 22);
    store(bytes + 1, 0);
    CHECK(MASK(2, input, output, 8) == 0 && word(bytes + 17) == word(bytes + 128));
    CHECK(MASK(999, 0, output, 8) == 0 && word(bytes + 17) == 0);
    store(bytes + 1, 0x200); // SIGUSR1.
    CHECK(MASK(0, input, output, 8) == 0 && word(bytes + 17) == 0);
    store(bytes + 1, 0x800); // SIGUSR2.
    CHECK(MASK(0, input, output, 8) == 0 && word(bytes + 17) == 0x200);
    store(bytes + 1, 0x200);
    CHECK(MASK(1, input, output, 8) == 0 && word(bytes + 17) == 0xa00);
    store(bytes + 1, UINT64_MAX);
    CHECK(MASK(2, input, output, 8) == 0 && word(bytes + 17) == 0x800);
    CHECK(MASK(2, 0, output, 8) == 0 && word(bytes + 17) == (UINT64_MAX & ~UINT64_C(0x40100)));
    store(bytes + 1, 0);
    CHECK(MASK(2, input, 1, 8) == -1 && *artbox_stub___errno() == 14);
    CHECK(MASK(2, 0, output, 8) == 0 && word(bytes + 17) == 0); // Change precedes a bad old-mask write.
    CHECK(MASK(999, 1, output, 8) == -1 && *artbox_stub___errno() == 14);
    CHECK(MASK(999, input, output, 8) == -1 && *artbox_stub___errno() == 22);
    CHECK(MASK(2, 0, 1, 8) == -1 && *artbox_stub___errno() == 14);
    store(bytes + 1, 0x200);
    CHECK(MASK(2, input, input, 8) == 0 && word(bytes + 1) == 0); // Aliased input/output.
    CHECK(MASK(2, 0, output, 8) == 0 && word(bytes + 17) == 0x200);
    CHECK(MASK(2, saved, 0, 8) == 0);
    CHECK(CALL(215, address, page * 2, 0) == 0);
    return cases; // 53 cases, with byte layout independent of host time_t.
}
