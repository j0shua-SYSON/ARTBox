#include "artbox/native_syscall.h"
#include <errno.h>

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

int64_t artbox_bionic_syscall(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5) {
    const artbox_syscall_binding* binding = current_binding;
    int saved_errno;
    int64_t result;
    if (!binding || !binding->dispatch) return -38;
    saved_errno = errno;
    result = binding->dispatch(binding->context, number, a0, a1, a2, a3, a4, a5);
    errno = saved_errno;
    return result;
}
