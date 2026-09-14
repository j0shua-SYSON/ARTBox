// Original caller, MIT. The same NDK object runs with Bionic on both hosts.
#include <stdint.h>
#include <stddef.h>
extern int64_t artbox_futex_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_futex_errno(void);
static int64_t call(uint64_t word, uint64_t op, uint64_t value, uint64_t timeout, uint64_t mask) {
    return artbox_futex_syscall(98, word, op, value, timeout, 0, mask);
}
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)
#define ERROR(c, e) ((c) == -1 && *artbox_futex_errno() == (e))
int64_t artbox_futex_check(void *scratch) {
    uint64_t *storage = scratch;
    storage[0] = 1;
    int64_t *ts = (int64_t *)(storage + 2);
    uint64_t word = (uintptr_t)storage, timeout = (uintptr_t)ts;
    unsigned cases = 0;
    ts[0] = ts[1] = 0;
    CHECK(ERROR(call(word, 128, 0, 0, 0), 11));
    CHECK(call(word, 129, 1, 0, 0) == 0 && *artbox_futex_errno() == 11);
    CHECK(ERROR(call(word, 128, 1, timeout, 0), 110));
    ts[0] = -1;
    CHECK(ERROR(call(word, 128, 0, timeout, 0), 22));
    ts[0] = 0; ts[1] = 1000000000;
    CHECK(ERROR(call(word, 128, 1, timeout, 0), 22));
    CHECK(ERROR(call(word, 128, 0, 1, 0), 14));
    CHECK(ERROR(call(word, 128 | 256, 1, 0, 0), 38));
    CHECK(ERROR(call(word, 137, 1, 0, 0), 22));
    CHECK(ERROR(call(word, 138, 1, 0, 0), 22));
    CHECK(ERROR(call(word + 1, 128, 1, 0, 0), 22));
    CHECK(ERROR(call(word + 1, 129, 1, 0, 0), 22));
    CHECK(ERROR(call(0, 128, 0, 0, 0), 14));
    CHECK(call(0, 129, 1, 0, 0) == 0);
    CHECK(ERROR(call(word, 127, 1, 0, 0), 38));
    ts[0] = ts[1] = 0;
    CHECK(ERROR(call(word, 137, 1, timeout, UINT32_MAX), 110));
    CHECK(ERROR(call(word, 137 | 256, 1, timeout, UINT32_MAX), 110));
    CHECK(call(word, 138, 0, 0, 1) == 0);
    CHECK(storage[0] == 1);
    ts[1] = 1000000;
    CHECK(ERROR(call(word, 128, 1, timeout, 0), 110));
    return cases;
}
