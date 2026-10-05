/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "artbox/binder_libc_result.h"
#include <errno.h>
#include <fnmatch.h>
#include <stdint.h>
#include <time.h>

#define CHECK_AT(id, value) do { if (!(value)) return -(id); ++cases; } while (0)

static int ordered(struct timespec a, struct timespec b) {
    return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec <= b.tv_nsec);
}

int artbox_binder_libc_check(unsigned mutation) {
    static const struct {
        const char *pattern, *value;
        int flags, expected;
    } patterns[] = {
        {"", "", 0, 0}, {"", "a", 0, FNM_NOMATCH},
        {"*", "", 0, 0}, {"?", "", 0, FNM_NOMATCH},
        {"?", "a", 0, 0}, {"?", "ab", 0, FNM_NOMATCH},
        {"a*b*c", "axbyc", 0, 0}, {"a*b*c", "abx", 0, FNM_NOMATCH},
        {"a/*", "a/b/c", FNM_PATHNAME, FNM_NOMATCH},
        {"a/*", "a/b/c", 0, 0},
        {"*", ".hidden", FNM_PERIOD, FNM_NOMATCH},
        {".*", ".hidden", FNM_PERIOD, 0},
        {"[.]hidden", ".hidden", FNM_PERIOD, FNM_NOMATCH},
        {"*/.*", "a/.b", FNM_PATHNAME | FNM_PERIOD, 0},
        {"*/*", "a/.b", FNM_PATHNAME | FNM_PERIOD, FNM_NOMATCH},
        {"*", "a/b", FNM_PATHNAME, FNM_NOMATCH},
        {"a/?", "a/b", FNM_PATHNAME, 0},
        {"a/?", "a/bc", FNM_PATHNAME, FNM_NOMATCH},
        {"foo", "foo/bar", FNM_LEADING_DIR, 0},
        {"foo", "foobar", FNM_LEADING_DIR, FNM_NOMATCH},
        {"*.SO", "libbinder.so", FNM_CASEFOLD, 0},
        {"*.SO", "libbinder.so", 0, FNM_NOMATCH},
        {"[a-c]??", "box", 0, 0}, {"[a-c]??", "fox", 0, FNM_NOMATCH},
        {"[!a-c]", "z", 0, 0}, {"[!a-c]", "b", 0, FNM_NOMATCH},
        {"\\*", "*", 0, 0}, {"\\*", "abc", 0, FNM_NOMATCH},
        {"\\*", "\\abc", FNM_NOESCAPE, 0}, {"foo\\?", "foo?", 0, 0},
        {"[abc", "[abc", 0, 0}, {"a**b", "ab", 0, 0},
        {"a**b", "acccb", 0, 0}, {"[A-C]", "b", FNM_CASEFOLD, 0},
        {"a//b", "a/b", FNM_PATHNAME, FNM_NOMATCH},
        {"a*", "b", 0, FNM_NOMATCH}
    };
    _Static_assert(sizeof(patterns) / sizeof(patterns[0]) + 14 == ARTBOX_BINDER_LIBC_CASES,
                   "Every expectation must remain in the contract");
    int cases = 0;
    if (mutation > 2) return -1;
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        int flags = patterns[i].flags;
        if (mutation == 1) flags &= ~FNM_PATHNAME;
        CHECK_AT(100 + (int)i, fnmatch(patterns[i].pattern, patterns[i].value, flags) == patterns[i].expected);
    }

    const int bases[] = { TIME_UTC, TIME_MONOTONIC };
    const clockid_t clocks[] = { CLOCK_REALTIME, CLOCK_MONOTONIC };
    const uint64_t guard = UINT64_C(0x8192736455aabbcc);
    for (unsigned i = 0; i < 2; ++i) {
        struct timespec before, after;
        struct { uint64_t before; struct timespec value; uint64_t after; } output =
            { guard, { -7, -9 }, guard };
        CHECK_AT(200 + (int)i * 10, clock_gettime(clocks[i], &before) == 0);
        errno = E2BIG;
        const int base = mutation == 2 ? TIME_UTC : bases[i];
        CHECK_AT(201 + (int)i * 10, timespec_get(&output.value, base) == bases[i]);
        CHECK_AT(202 + (int)i * 10, output.before == guard && output.after == guard &&
                 output.value.tv_sec >= 0 && output.value.tv_nsec >= 0 && output.value.tv_nsec < 1000000000);
        CHECK_AT(203 + (int)i * 10, clock_gettime(clocks[i], &after) == 0);
        CHECK_AT(204 + (int)i * 10, ordered(before, output.value) && ordered(output.value, after));
        CHECK_AT(205 + (int)i * 10, errno == E2BIG);
    }
    const int invalid[] = { 0, 1024 };
    for (unsigned i = 0; i < 2; ++i) {
        struct timespec output = { -7, -9 };
        errno = 0;
        CHECK_AT(500 + (int)i, timespec_get(&output, invalid[i]) == 0 && errno == EINVAL &&
                 output.tv_sec == -7 && output.tv_nsec == -9);
    }
    return cases;
}
