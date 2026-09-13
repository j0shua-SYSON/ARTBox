#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern int64_t artbox_vm_check(size_t page);
extern int64_t artbox_system_check(size_t page, uint64_t pid, uint64_t tid);
static _Thread_local int guest_errno;
int *artbox_stub___errno(void) { return &guest_errno; }

int64_t artbox_stub_artbox_bionic_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5) {
    /* Only the fixture's nine operations may enter the real Linux ABI. */
    if (n != 222 && n != 226 && n != 215 && n != 233 && n != 172 && n != 178 &&
        n != 96 && n != 113 && n != 278) return -38;
    int saved = errno;
    long result = syscall(n, a0, a1, a2, a3, a4, a5);
    int error = errno;
    errno = saved;
    return result == -1 ? -error : result;
}

int main(void) {
    struct timespec start, vm_end, end;
    long page = sysconf(_SC_PAGESIZE);
    if (page < 4096 || clock_gettime(CLOCK_MONOTONIC, &start)) return 2;
    errno = EDOM;
    int64_t cases = artbox_vm_check((size_t)page);
    if (clock_gettime(CLOCK_MONOTONIC, &vm_end)) return 2;
    int64_t system_cases = artbox_system_check((size_t)page, (uint64_t)getpid(), (uint64_t)gettid());
    if (cases != 35 || system_cases != 36 || errno != EDOM) {
        fprintf(stderr, "VM/system oracle: %" PRId64 "/%" PRId64 " (negative source line on failure)\n", cases, system_cases);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end)) return 2;
    int64_t elapsed = (vm_end.tv_sec - start.tv_sec) * INT64_C(1000000000) + vm_end.tv_nsec - start.tv_nsec;
    int64_t system_elapsed = (end.tv_sec - vm_end.tv_sec) * INT64_C(1000000000) + end.tv_nsec - vm_end.tv_nsec;
    printf("{\"cases\":35,\"page_size\":%ld,\"elapsed_ns\":%" PRId64 ",\"system_cases\":36,\"system_elapsed_ns\":%" PRId64 "}\n",
           page, elapsed, system_elapsed);
    return 0;
}
