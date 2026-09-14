// Original NDK /proc/self/cmdline contract, MIT. Same object on both runtimes.
#include <stdint.h>
#include <stddef.h>
extern int64_t artbox_file_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_file_errno(void);
#define CALL(n,a,b,c,d,e,f) artbox_file_syscall(n,(uint64_t)(a),(uint64_t)(b),(uint64_t)(c),(uint64_t)(d),(uint64_t)(e),(uint64_t)(f))
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)
#define ERROR(c,e) ((c) == -1 && *artbox_file_errno() == (e))
static uint64_t word(const unsigned char *p, unsigned n) {
    uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i) value |= (uint64_t)p[i] << (i*8);
    return value;
}
static int equal(const unsigned char *a, const unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
int64_t artbox_proc_check(const unsigned char *expected, size_t length, size_t page) {
    unsigned cases = 0;
    unsigned char bytes[1024], st[128];
    if (!length || length > sizeof(bytes) || !expected || expected[length-1]) return -1;
    int64_t fd = CALL(56, -100, "/proc/self/cmdline", 0, 0, 0, 0);
    CHECK(fd >= 3);
    CHECK(CALL(80, fd, st, 0, 0, 0, 0) == 0 && (word(st+16, 4) & 0170000) == 0100000 && word(st+48, 8) == 0);
    CHECK(CALL(63, fd, bytes, 1, 0, 0, 0) == 1 && bytes[0] == expected[0]);
    CHECK(CALL(63, fd, bytes+1, sizeof(bytes)-1, 0, 0, 0) == (int64_t)length-1);
    CHECK(equal(bytes, expected, length));
    CHECK(CALL(63, fd, 0, 1, 0, 0, 0) == 0);
    CHECK(CALL(62, fd, 0, 2, 0, 0, 0) == 0); // Proc stat size is zero, independent of readable bytes.
    CHECK(CALL(63, fd, bytes, sizeof(bytes), 0, 0, 0) == (int64_t)length && equal(bytes, expected, length));
    CHECK(ERROR(CALL(62, fd, -1, 0, 0, 0, 0), 22));
    CHECK(CALL(62, fd, 0, 1, 0, 0, 0) == (int64_t)length);
    CHECK(CALL(62, fd, 0, 0, 0, 0, 0) == 0);
    CHECK(ERROR(CALL(63, fd, 0, 1, 0, 0, 0), 14) && CALL(62, fd, 0, 1, 0, 0, 0) == 0);
    CHECK(ERROR(CALL(64, fd, bytes, 1, 0, 0, 0), 9));
    CHECK(ERROR(CALL(222, 0, page, 1, 2, fd, 0), 19));
    CHECK(CALL(57, fd, 0, 0, 0, 0, 0) == 0);
    CHECK(ERROR(CALL(57, fd, 0, 0, 0, 0, 0), 9));
    int64_t directory = CALL(56, -100, "/proc", 0x4000, 0, 0, 0);
    CHECK(directory >= 3);
    fd = CALL(56, directory, "self/cmdline", 0, 0, 0, 0);
    CHECK(fd >= 3);
    CHECK(CALL(63, fd, bytes, sizeof(bytes), 0, 0, 0) == (int64_t)length && equal(bytes, expected, length));
    CHECK(CALL(57, fd, 0, 0, 0, 0, 0) == 0 && CALL(57, directory, 0, 0, 0, 0, 0) == 0);
    CHECK(ERROR(CALL(56, -100, "/proc/self/artbox-missing", 0, 0, 0, 0), 2));
    CHECK(CALL(79, -100, "/proc/self", st, 0, 0, 0) == 0 && (word(st+16, 4) & 0170000) == 0040000);
    return cases; // 22; freeze before implementing the virtual proc nodes.
}
