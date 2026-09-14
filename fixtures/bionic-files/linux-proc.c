#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int64_t artbox_proc_check(const unsigned char *, size_t, size_t);
extern int64_t artbox_stub_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
int *artbox_stub___errno(void) { return &errno; }
int *artbox_file_errno(void) { return &errno; }
int64_t artbox_file_syscall(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    return artbox_stub_syscall(n, a, b, c, d, e, f);
}
int64_t artbox_stub_artbox_bionic_syscall(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    if (n != 56 && n != 57 && n != 62 && n != 63 && n != 64 && n != 79 && n != 80 && n != 222) return -38;
    int saved = errno;
    long result = syscall(n, a, b, c, d, e, f);
    int error = errno; errno = saved;
    return result == -1 ? -error : result;
}
int main(int argc, char **argv) {
    if (argc != 1) return 2;
    int64_t cases = artbox_proc_check((const unsigned char *)argv[0], strlen(argv[0])+1, (size_t)sysconf(_SC_PAGESIZE));
    printf("{\"proc_cases\":%lld}\n", (long long)cases);
    return cases == 22 ? 0 : 1;
}
