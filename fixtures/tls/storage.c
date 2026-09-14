// Original NDK ELF TLS storage, MIT. Exported from a separate shared library.
#include <stdint.h>
_Thread_local uint64_t artbox_tls_initialized = UINT64_C(0x123456789abcdef0);
_Alignas(32) _Thread_local uint64_t artbox_tls_zero[17];
