#ifndef ARTBOX_NATIVE_SYSCALL_H
#define ARTBOX_NATIVE_SYSCALL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t (*artbox_syscall_handler)(void*, uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t);
typedef struct artbox_syscall_binding {
    artbox_syscall_handler dispatch;
    void* context;
} artbox_syscall_binding;

/* Borrowed, immutable binding for this native thread. The owner restores the
 * previous binding after nested entry or non-local guest exit, and keeps both
 * binding and context alive until then. New threads start unbound. Guest TLS
 * construction/binding remains a separate runtime responsibility. */
const artbox_syscall_binding* artbox_native_syscall_swap(const artbox_syscall_binding* binding);

/* Precompiled ARM64-compatible endpoint: number plus six complete 64-bit words.
 * Return raw Linux values; never perform libc's errno/-1 conversion here.
 * Returning calls preserve host errno. A missing handler returns -ENOSYS.
 * The dispatcher must not modify guest errno or forward Linux numbers to Darwin. */
int64_t artbox_bionic_syscall(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5);

#ifdef __cplusplus
}
#endif
#endif
