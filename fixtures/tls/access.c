// Original TLS acceptance source, MIT. Original ELF runs on Linux; the signed
// build changes only compiler-emitted TP reads in this source's assembly.
#include <stdint.h>
extern _Thread_local uint64_t artbox_tls_initialized;
extern _Thread_local uint64_t artbox_tls_zero[17];
_Alignas(64) static _Thread_local uint64_t local[4] = {19};
__attribute__((noinline)) static uint64_t *initialized_address(void) { return &artbox_tls_initialized; }
__attribute__((noinline)) static uint64_t *zero_address(void) { return artbox_tls_zero; }
__attribute__((noinline)) static uint64_t *local_address(void) { return local; }

int artbox_tls_check(unsigned id, unsigned phase) {
    uint64_t *a = initialized_address(), *b = zero_address(), *c = local_address();
    if (!a || !b || !c || (uintptr_t)b % 32 || (uintptr_t)c % 64) return -1;
    if (!phase) {
        if (*a != UINT64_C(0x123456789abcdef0) || c[0] != 19) return -2;
        for (unsigned i = 0; i < 17; ++i) if (b[i]) return -3;
        for (unsigned i = 1; i < 4; ++i) if (c[i]) return -4;
        *a = 1000 + id;
        for (unsigned i = 0; i < 17; ++i) b[i] = id * 100 + i + 1;
        for (unsigned i = 0; i < 4; ++i) c[i] = id * 10 + i + 1;
    } else {
        if (*a != 1000 + id) return -5;
        for (unsigned i = 0; i < 17; ++i) if (b[i] != id * 100 + i + 1) return -6;
        for (unsigned i = 0; i < 4; ++i) if (c[i] != id * 10 + i + 1) return -7;
    }
    return 0;
}
