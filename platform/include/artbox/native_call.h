#ifndef ARTBOX_NATIVE_CALL_H
#define ARTBOX_NATIVE_CALL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Precompiled AArch64 register bridge for verified guest entry points using
 * at most seven integer/pointer words and an x0 result. It does not implement
 * a general variadic/floating-point ABI. Entry, TLS and imports must be ready. */
uint64_t artbox_call7(const void* entry, uint64_t a0, uint64_t a1, uint64_t a2,
                     uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);
#ifdef __cplusplus
}
#endif
#endif
