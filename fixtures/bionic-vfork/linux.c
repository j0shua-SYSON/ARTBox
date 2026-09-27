/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
extern int artbox_vfork_check(void);
int main(int argc, char **argv) {
    int mutation = argc == 2 && !strcmp(argv[1], "--expect-failure");
    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return 2;
    int result = artbox_vfork_check();
    if (clock_gettime(CLOCK_MONOTONIC, &end)) return 2;
    int64_t elapsed = (int64_t)(end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec;
    printf("{\"result\":%d,\"mutation\":%s,\"elapsed_ns\":%" PRId64 "}\n",
           result, mutation ? "true" : "false", elapsed);
    return result == (mutation ? -1000 : 28) ? 0 : 1;
}
