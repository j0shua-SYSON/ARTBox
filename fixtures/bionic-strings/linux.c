#define _GNU_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int64_t artbox_string_check(unsigned char*, unsigned char*, size_t);

int main(void) {
    long page = sysconf(_SC_PAGESIZE);
    struct timespec before, after;
    if (page < 4096) return 2;
    unsigned char* a = mmap(NULL, (size_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char* b = mmap(NULL, (size_t)page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (a == MAP_FAILED || b == MAP_FAILED || mprotect(a + page, (size_t)page, PROT_READ | PROT_WRITE) ||
        mprotect(b + page, (size_t)page, PROT_READ | PROT_WRITE) || clock_gettime(CLOCK_MONOTONIC, &before)) return 2;
    int64_t cases = artbox_string_check(a + page, b + page, (size_t)page);
    if (clock_gettime(CLOCK_MONOTONIC, &after) || munmap(a, (size_t)page * 3) || munmap(b, (size_t)page * 3)) return 2;
    if (cases != 35908) { fprintf(stderr, "string check: %" PRId64 " (negative source line on failure)\n", cases); return 1; }
    uint64_t ns = (uint64_t)(after.tv_sec - before.tv_sec) * UINT64_C(1000000000) +
                  (uint64_t)(after.tv_nsec - before.tv_nsec);
    printf("{\"cases\":35908,\"page_size\":%ld,\"elapsed_ns\":%" PRIu64 "}\n", page, ns);
    return 0;
}
