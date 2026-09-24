// SPDX-License-Identifier: MIT
// Fixed vectors exercise the actual Android libm calls, including signed zero,
// exceptional inputs, subnormals and large-argument reduction. This is not an
// exhaustive accuracy or floating-point-environment conformance suite.
#include <math.h>
#include <stdint.h>

#ifdef ARTBOX_HOST_REFERENCE
#define artbox_math_check artbox_host_math_check
#define artbox_math_case_count artbox_host_math_case_count
#endif

enum operation {
    ACOS, ASIN, ATAN, ATAN2, CBRT, COS, COSH, EXP, EXPM1, FMOD,
    FMODF, HYPOT, LOG, LOG10, NEXTAFTER, POW, SIN, SINH, TAN, TANH
};
enum expectation { EXACT, CLOSE, NAN_RESULT };
struct vector {
    enum operation op;
    enum expectation kind;
    double a, b, expected;
};
#define E(op, a, b, value) {op, EXACT, a, b, value}
#define C(op, a, b, value) {op, CLOSE, a, b, value}
#define N(op, a, b) {op, NAN_RESULT, a, b, 0}
static const struct vector vectors[] = {
    E(ACOS, 1, 0, 0), C(ACOS, 0, 0, 0x1.921fb54442d18p+0), N(ACOS, 2, 0),
    E(ASIN, -0.0, 0, -0.0), C(ASIN, 1, 0, 0x1.921fb54442d18p+0), N(ASIN, 2, 0),
    E(ATAN, -0.0, 0, -0.0), C(ATAN, 1, 0, 0x1.921fb54442d18p-1),
    C(ATAN, INFINITY, 0, 0x1.921fb54442d18p+0),
    E(ATAN2, -0.0, 1, -0.0), C(ATAN2, 0, -1, 0x1.921fb54442d18p+1),
    C(ATAN2, -0.0, -1, -0x1.921fb54442d18p+1),
    E(CBRT, -0.0, 0, -0.0), C(CBRT, -8, 0, -2), C(CBRT, 27, 0, 3),
    E(COS, 0, 0, 1), C(COS, 1, 0, 0x1.14a280fb5068cp-1),
    C(COS, 1e20, 0, 0x1.872720fc60d3dp-1), N(COS, INFINITY, 0),
    E(COSH, 0, 0, 1), C(COSH, 1, 0, 0x1.8b07551d9f550p+0),
    E(COSH, -INFINITY, 0, INFINITY),
    E(EXP, 0, 0, 1), C(EXP, 1, 0, 0x1.5bf0a8b145769p+1),
    E(EXP, -INFINITY, 0, 0), E(EXP, INFINITY, 0, INFINITY),
    E(EXPM1, -0.0, 0, -0.0), E(EXPM1, 0x1p-60, 0, 0x1p-60),
    E(EXPM1, -INFINITY, 0, -1), C(EXPM1, 1, 0, 0x1.b7e151628aed2p+0),
    E(FMOD, -5.5, 2, -1.5), E(FMOD, -4, 2, -0.0),
    E(FMOD, 5, INFINITY, 5), N(FMOD, 1, 0), N(FMOD, INFINITY, 1),
    E(FMODF, -5.5, 2, -1.5), E(FMODF, -4, 2, -0.0), N(FMODF, 1, 0),
    E(HYPOT, 3, 4, 5), E(HYPOT, 0, -0.0, 0),
    E(HYPOT, INFINITY, NAN, INFINITY), C(HYPOT, 0x1.8p+500, 0x1p+502, 0x1.11687a8ae14a3p+502),
    E(LOG, 1, 0, 0), C(LOG, 2, 0, 0x1.62e42fefa39efp-1),
    E(LOG, 0, 0, -INFINITY), N(LOG, -1, 0), E(LOG, INFINITY, 0, INFINITY),
    E(LOG10, 1, 0, 0), C(LOG10, 1000, 0, 3), E(LOG10, 0, 0, -INFINITY),
    E(NEXTAFTER, 0, 1, 0x1p-1074), E(NEXTAFTER, 0, -1, -0x1p-1074),
    E(NEXTAFTER, 1, 2, 0x1.0000000000001p+0), E(NEXTAFTER, 0x1p-1074, 0, 0),
    E(NEXTAFTER, 0, -0.0, -0.0),
    E(POW, 2, 10, 1024), E(POW, -2, 3, -8), E(POW, NAN, 0, 1),
    E(POW, 1, NAN, 1), E(POW, -0.0, 3, -0.0), E(POW, -0.0, -3, -INFINITY), N(POW, -2, 0.5),
    E(SIN, -0.0, 0, -0.0), E(SIN, 0x1p-1074, 0, 0x1p-1074),
    C(SIN, 1, 0, 0x1.aed548f090ceep-1), C(SIN, 1e20, 0, -0x1.4a5e605fd6450p-1), N(SIN, INFINITY, 0),
    E(SINH, -0.0, 0, -0.0), C(SINH, 1, 0, 0x1.2cd9fc44eb982p+0),
    E(SINH, -INFINITY, 0, -INFINITY),
    E(TAN, -0.0, 0, -0.0), C(TAN, 1, 0, 0x1.8eb245cbee3a6p+0), N(TAN, INFINITY, 0),
    E(TANH, -0.0, 0, -0.0), C(TANH, 1, 0, 0x1.85efab514f394p-1),
    E(TANH, INFINITY, 0, 1), E(TANH, -INFINITY, 0, -1), N(TANH, NAN, 0)
};

static uint64_t bits(double value) {
    union { double value; uint64_t bits; } u = { .value = value };
    return u.bits;
}
static double evaluate(enum operation op, double a, double b) {
    switch (op) {
    case ACOS: return acos(a); case ASIN: return asin(a); case ATAN: return atan(a);
    case ATAN2: return atan2(a, b); case CBRT: return cbrt(a); case COS: return cos(a);
    case COSH: return cosh(a); case EXP: return exp(a); case EXPM1: return expm1(a);
    case FMOD: return fmod(a, b); case FMODF: return fmodf((float)a, (float)b);
    case HYPOT: return hypot(a, b); case LOG: return log(a); case LOG10: return log10(a);
    case NEXTAFTER: return nextafter(a, b); case POW: return pow(a, b);
    case SIN: return sin(a); case SINH: return sinh(a); case TAN: return tan(a); case TANH: return tanh(a);
    }
    return NAN;
}
uint32_t artbox_math_case_count(void) { return sizeof(vectors) / sizeof(vectors[0]); }
uint32_t artbox_math_check(void) {
    for (uint32_t i = 0; i < artbox_math_case_count(); ++i) {
        const struct vector *v = &vectors[i];
        uint64_t actual = bits(evaluate(v->op, v->a, v->b)), expected = bits(v->expected);
        if (v->kind == NAN_RESULT) {
            if ((actual & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
                (actual & UINT64_C(0x000fffffffffffff))) continue;
        } else if (actual == expected) {
            continue;
        } else if (v->kind == CLOSE && (actual >> 63) == (expected >> 63) &&
                   (actual > expected ? actual - expected : expected - actual) <= 2) {
            continue;
        }
        return i + 1;
    }
    return 0;
}
