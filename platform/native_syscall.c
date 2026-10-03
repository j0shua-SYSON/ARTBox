#include "artbox/native_syscall.h"
#include <errno.h>
#if defined(__APPLE__) && defined(__aarch64__)
#include "artbox/native_signal_binding.h"
#endif

#if defined(_MSC_VER)
static __declspec(thread) const artbox_syscall_binding* current_binding;
#else
static _Thread_local const artbox_syscall_binding* current_binding;
#endif

const artbox_syscall_binding* artbox_native_syscall_swap(const artbox_syscall_binding* binding) {
    const artbox_syscall_binding* previous = current_binding;
    current_binding = binding;
    return previous;
}

static int64_t invoke(const artbox_syscall_binding *binding,uint64_t number,uint64_t a0,uint64_t a1,
                      uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    int saved_errno;
    int64_t result;
    if (!binding || !binding->dispatch) return -38;
    saved_errno = errno;
    result = binding->dispatch(binding->context, number, a0, a1, a2, a3, a4, a5);
    errno = saved_errno;
    return result;
}
#if defined(__APPLE__) && defined(__aarch64__)
__attribute__((noinline))
#endif
static int64_t ordinary_syscall(uint64_t number,uint64_t a0,uint64_t a1,uint64_t a2,
                               uint64_t a3,uint64_t a4,uint64_t a5) {
    return invoke(current_binding,number,a0,a1,a2,a3,a4,a5);
}
int64_t artbox_bionic_syscall(uint64_t number,uint64_t a0,uint64_t a1,uint64_t a2,
                            uint64_t a3,uint64_t a4,uint64_t a5) {
#if defined(__APPLE__) && defined(__aarch64__)
    // Compiler-TLS resolution stays in the non-inlined ordinary path. A
    // handler must not enter it while mapper or logging locks are held.
    const artbox_native_signal_scope *scope=artbox_native_signal_current();
    if(scope) return invoke(&scope->syscall,number,a0,a1,a2,a3,a4,a5);
#endif
    return ordinary_syscall(number,a0,a1,a2,a3,a4,a5);
}
