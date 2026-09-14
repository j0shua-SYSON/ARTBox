/* The same NDK object runs through signed Bionic on Darwin and real Linux.
 * No host libc calls, allocation, or runtime-generated code in this caller. */
#include <stddef.h>
#include <stdint.h>

extern int64_t artbox_stub_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_stub___errno(void);
#define CHECK(x) do { if (!(x)) return -__LINE__; ++cases; } while (0)
#define MAP(a, n, p, f) artbox_stub_syscall(222, a, n, p, f, UINT64_MAX, 0)
#define PROTECT(a, n, p) artbox_stub_syscall(226, a, n, p, 0, 0, 0)
#define UNMAP(a, n) artbox_stub_syscall(215, a, n, 0, 0, 0, 0)
#define ADVISE(a, n, v) artbox_stub_syscall(233, a, n, v, 0, 0, 0)

int64_t artbox_vm_check(size_t page) {
    int64_t cases = 0;
    if (page < 4096 || page > 65536 || (page & (page - 1))) return -1;
    CHECK(MAP(0, 0, 3, 0x22) == -1 && *artbox_stub___errno() == 22);
    int64_t address = MAP(0, page * 3 - 1, 3, 0x22);
    CHECK(address > 0 && (uint64_t)address % page == 0);
    uint64_t base = (uint64_t)address;
    volatile unsigned char *bytes = (volatile unsigned char *)(uintptr_t)base;
    for (size_t i = 0; i < page * 3; ++i) if (bytes[i]) return -__LINE__;
    ++cases;
    for (size_t i = 0; i < page * 3; ++i) bytes[i] = (unsigned char)(1 + i / page);
    CHECK(PROTECT(base + 1, page, 1) == -1 && *artbox_stub___errno() == 22);
    CHECK(PROTECT(base + page, page - 1, 1) == 0);
    CHECK(bytes[page] == 2 && bytes[page * 2 - 1] == 2);
    CHECK(PROTECT(base, page, 0) == 0);
    CHECK(PROTECT(base, page, 3) == 0 && bytes[0] == 1);
    CHECK(UNMAP(base + page, page) == 0);
    CHECK(PROTECT(base + page, page, 1) == -1 && *artbox_stub___errno() == 12);
    CHECK(bytes[0] == 1 && bytes[2 * page] == 3);
    CHECK(MAP(base + page, page, 3, 0x4032) == address + (int64_t)page);
    for (size_t i = page; i < page * 2; ++i) if (bytes[i]) return -__LINE__;
    ++cases;
    for (size_t i = page; i < page * 2; ++i) bytes[i] = 4;
    CHECK(PROTECT(base + page, page, 1) == 0);
    CHECK(ADVISE(base + page, page, 4) == 0);
    for (size_t i = page; i < page * 2; ++i) if (bytes[i]) return -__LINE__;
    ++cases;
    CHECK(bytes[0] == 1 && bytes[2 * page] == 3);
    CHECK(UNMAP(base + page, page) == 0);
    CHECK(UNMAP(base, page * 3) == 0);
    CHECK(UNMAP(base, page * 3) == 0);
    CHECK(PROTECT(base, 0, 1) == 0);
    CHECK(UNMAP(base, 0) == -1 && *artbox_stub___errno() == 22);
    address = MAP(0, page * 8, 0, 0x4022);
    CHECK(address > 0);
    base = (uint64_t)address;
    CHECK(MAP(base + page * 2, page * 2, 3, 0x32) == address + (int64_t)(page * 2));
    bytes = (volatile unsigned char *)(uintptr_t)(base + page * 2);
    for (size_t i = 0; i < page * 2; ++i) if (bytes[i]) return -__LINE__;
    ++cases;
    for (size_t i = 0; i < page * 2; ++i) bytes[i] = 5;
    CHECK(MAP(base + page * 2, page, 3, 0x32) == address + (int64_t)(page * 2));
    CHECK(bytes[0] == 0 && bytes[page] == 5);
    CHECK(UNMAP(base, page * 2) == 0);
    CHECK(UNMAP(base + page * 4, page * 4) == 0);
    CHECK(bytes[page] == 5);
    CHECK(UNMAP(base + page * 2, page * 2) == 0);
    address = MAP(0, page, 2, 0x22);
    CHECK(address > 0);
    bytes = (volatile unsigned char *)(uintptr_t)address;
    bytes[0] = 6; // Linux ARM64 writable pages are readable as well.
    CHECK(bytes[0] == 6);
    CHECK(ADVISE((uint64_t)address, page, 0) == 0);
    CHECK(UNMAP((uint64_t)address, page) == 0);
    return cases; // 35 operations/byte-range expectations, independent of page size.
}
