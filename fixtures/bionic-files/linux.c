#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
extern int64_t artbox_files_check(uint64_t);
extern int64_t artbox_file_mapping_check(uint64_t);
extern int64_t artbox_stub_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static _Thread_local int guest_errno;
int *artbox_stub___errno(void) { return &guest_errno; }
int *artbox_file_errno(void) { return &guest_errno; }
int64_t artbox_file_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                           uint64_t a3, uint64_t a4, uint64_t a5) {
    return artbox_stub_syscall(n, a0, a1, a2, a3, a4, a5);
}
int64_t artbox_stub_artbox_bionic_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5) {
    if (n != 56 && n != 57 && n != 62 && n != 63 && n != 64 && n != 79 && n != 80 && n != 215 && n != 222 && n != 226 && n != 227 && n != 233)
        return -38;
    int saved = errno;
    long value = syscall(n, a0, a1, a2, a3, a4, a5);
    int error = errno; errno = saved;
    return value == -1 ? -error : value;
}
int main(void) {
    struct timespec start, end;
    long page = sysconf(_SC_PAGESIZE);
    umask(022);
    errno = EDOM;
    if (page < 4096 || clock_gettime(CLOCK_MONOTONIC, &start)) return 2;
    int64_t cases = artbox_files_check((uint64_t)page);
    if (cases != 41 || errno != EDOM) {
        fprintf(stderr, "Bionic file caller: %" PRId64 " (negative source line on failure)\n", cases);
        return 1;
    }
    int64_t mappings = artbox_file_mapping_check((uint64_t)page);
    if (mappings != 43 || errno != EDOM) {
        fprintf(stderr, "Bionic mapping caller: %" PRId64 " (negative source line on failure)\n", mappings);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end)) return 2;
    int64_t elapsed = (end.tv_sec-start.tv_sec)*INT64_C(1000000000)+end.tv_nsec-start.tv_nsec;
    printf("{\"cases\":41,\"mapping_cases\":43,\"elapsed_ns\":%" PRId64 "}\n", elapsed);
    return 0;
}
