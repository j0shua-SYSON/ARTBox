#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern int64_t artbox_vm_check(size_t page);
static _Thread_local int guest_errno;
int *artbox_stub___errno(void) { return &guest_errno; }

int64_t artbox_stub_artbox_bionic_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5) {
    /* Only the four fixture memory operations may enter the real Linux ABI. */
    if (n != 222 && n != 226 && n != 215 && n != 233) return -38;
    int saved = errno;
    long result = syscall(n, a0, a1, a2, a3, a4, a5);
    int error = errno;
    errno = saved;
    return result == -1 ? -error : result;
}

int main(void) {
    struct timespec start, end;
    long page = sysconf(_SC_PAGESIZE);
    if (page < 4096 || clock_gettime(CLOCK_MONOTONIC, &start)) return 2;
    errno = EDOM;
    int64_t cases = artbox_vm_check((size_t)page);
    if (cases != 35 || errno != EDOM) {
        fprintf(stderr, "VM oracle: %" PRId64 " (negative source line on failure)\n", cases);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end)) return 2;
    int64_t elapsed = (end.tv_sec - start.tv_sec) * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec;
    printf("{\"cases\":35,\"page_size\":%ld,\"elapsed_ns\":%" PRId64 "}\n", page, elapsed);
    return 0;
}
