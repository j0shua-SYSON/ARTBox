/* Integer encodings keep the test boundary independent of host long double. */
#include <stdint.h>
#include <stddef.h>

_Static_assert(sizeof(long double) == 16, "The Android ARM64 ABI uses binary128");
typedef union { long double value; uint64_t words[2]; } Quad;
extern int __eqtf2(long double, long double);
extern int __lttf2(long double, long double);
extern int __unordtf2(long double, long double);
extern long double __multf3(long double, long double);

static Quad bits(uint64_t high, uint64_t low) {
    Quad value = {.words = {low, high}};
    return value;
}

int64_t check(void) {
    /* Products encoded independently of the compiler's floating constant folder:
     * ordinary signs, zeros, infinities, under/overflow, subnormals and ties. */
    static const uint64_t products[][6] = {
        {0x3fff000000000000, 0, 0x4000000000000000, 0, 0x4000000000000000, 0},
        {0xbfff000000000000, 0, 0x4000000000000000, 0, 0xc000000000000000, 0},
        {0xbfff000000000000, 0, 0xc000000000000000, 0, 0x4000000000000000, 0},
        {0, 0, 0x3fff000000000000, 0, 0, 0},
        {0x8000000000000000, 0, 0x3fff000000000000, 0, 0x8000000000000000, 0},
        {0x7fff000000000000, 0, 0xbfff000000000000, 0, 0xffff000000000000, 0},
        {0x7ffeffffffffffff, UINT64_MAX, 0x4000000000000000, 0, 0x7fff000000000000, 0},
        {0x0001000000000000, 0, 0x3ffe000000000000, 0, 0x0000800000000000, 0},
        {0, 1, 0x4000000000000000, 0, 0, 2},
        {0, 1, 0x3ffe000000000000, 0, 0, 0},
        {0, 3, 0x3ffe000000000000, 0, 0, 2},
        {0x3fff000000000000, 1, 0x3fff000000000000, 1, 0x3fff000000000000, 2},
    };
    int64_t cases = 0;
    for (size_t i = 0; i < sizeof(products) / sizeof(products[0]); ++i) {
        const uint64_t *p = products[i];
        Quad a = bits(p[0], p[1]), b = bits(p[2], p[3]);
        Quad result = {.value = __multf3(a.value, b.value)};
        if (result.words[1] != p[4] || result.words[0] != p[5]) return -(int64_t)(100 + i);
        ++cases;
    }
    static const uint64_t ordered[][2] = {
        {0xffff000000000000, 0}, {0xc000000000000000, 0}, {0xbfff000000000000, 0},
        {0x8000000000000000, 1}, {0, 0}, {0, 1}, {0x3fff000000000000, 0},
        {0x3fff000000000000, 1}, {0x4000000000000000, 0}, {0x7fff000000000000, 0}
    };
    for (size_t i = 0; i < 10; ++i) for (size_t j = 0; j < 10; ++j) {
        Quad a = bits(ordered[i][0], ordered[i][1]), b = bits(ordered[j][0], ordered[j][1]);
        int order = __lttf2(a.value, b.value);
        if ((__eqtf2(a.value, b.value) == 0) != (i == j) || __unordtf2(a.value, b.value) ||
            (order < 0) != (i < j) || (order == 0) != (i == j) || (order > 0) != (i > j))
            return -(int64_t)(200 + i * 10 + j);
        ++cases;
    }
    Quad positive_zero = bits(0, 0), negative_zero = bits(0x8000000000000000, 0);
    if (__eqtf2(positive_zero.value, negative_zero.value) || __lttf2(positive_zero.value, negative_zero.value)) return -300;
    ++cases;
    Quad nan = bits(0x7fff800000000000, 123);
    for (size_t i = 0; i < 10; ++i) {
        Quad other = bits(ordered[i][0], ordered[i][1]);
        if (!__eqtf2(nan.value, other.value) || __lttf2(nan.value, other.value) <= 0 ||
            !__unordtf2(nan.value, other.value) || !__unordtf2(other.value, nan.value)) return -(int64_t)(400 + i);
        Quad result = {.value = __multf3(nan.value, other.value)};
        if ((result.words[1] & 0x7fff000000000000) != 0x7fff000000000000 ||
            !((result.words[1] & 0x0000ffffffffffff) | result.words[0])) return -(int64_t)(500 + i);
        ++cases;
    }
    return cases; // 123 independent products, ordered pairs, signed-zero and NaN cases.
}
