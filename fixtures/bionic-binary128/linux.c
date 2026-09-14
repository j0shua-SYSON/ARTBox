#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

extern int64_t artbox_quad_check(void);
int main(void) {
    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return 2;
    int64_t cases = artbox_quad_check();
    if (cases != 123) {
        fprintf(stderr, "binary128 check: %" PRId64 " (negative case index on failure)\n", cases);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end)) return 2;
    int64_t elapsed = (end.tv_sec - start.tv_sec) * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec;
    printf("{\"cases\":123,\"elapsed_ns\":%" PRId64 "}\n", elapsed);
    return 0;
}
