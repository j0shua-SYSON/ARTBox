#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
extern int64_t artbox_futex_check(void *);
extern int64_t artbox_stub_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static _Thread_local int guest_errno;
int *artbox_stub___errno(void) { return &guest_errno; }
int *artbox_futex_errno(void) { return &guest_errno; }
int64_t artbox_futex_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5) {
    return artbox_stub_syscall(n, a0, a1, a2, a3, a4, a5);
}
int64_t artbox_stub_artbox_bionic_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5) {
    if (n != 98) return -38;
    int saved = errno;
    long value = syscall(n, a0, a1, a2, a3, a4, a5);
    int error = errno;
    errno = saved;
    return value == -1 ? -error : value;
}
int main(void) {
    uint64_t scratch[4];
    errno = EDOM;
    int64_t cases = artbox_futex_check(scratch);
    if (cases != 19 || errno != EDOM) {
        fprintf(stderr, "Bionic futex caller: %" PRId64 " (negative source line on failure)\n", cases);
        return 1;
    }
    puts("{\"cases\":19}");
    return 0;
}
