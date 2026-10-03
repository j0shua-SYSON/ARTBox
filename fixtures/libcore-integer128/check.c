// SPDX-License-Identifier: MIT
#include <stdint.h>
typedef unsigned __int128 u128;
extern u128 __udivti3(u128, u128);
extern u128 __umodti3(u128, u128);
extern u128 __udivmodti4(u128, u128, u128*);
#include "vectors.h"
static u128 join(uint64_t hi, uint64_t lo) { return ((u128)hi << 64) | lo; }
int artbox_uint128_check(void) {
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const uint64_t *v = cases[i];
        u128 n = join(v[0], v[1]), d = join(v[2], v[3]);
        u128 q = join(v[4], v[5]), r = join(v[6], v[7]), actual_r = ~(u128)0;
        if (__udivti3(n, d) != q || __umodti3(n, d) != r ||
            __udivmodti4(n, d, &actual_r) != q || actual_r != r ||
            __udivmodti4(n, d, (u128*)0) != q) return -(int)(i + 1);
    }
    return sizeof(cases) / sizeof(cases[0]);
}
