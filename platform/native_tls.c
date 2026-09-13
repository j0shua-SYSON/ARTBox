#include "artbox/native_tls.h"

/* Use the host compiler's TLS ABI. In particular, a Darwin build does not use
 * Android's TPIDR_EL0 slot layout or change the host's thread pointer. */
#if defined(_MSC_VER)
static __declspec(thread) void **current_guest_tls;
#else
static _Thread_local void **current_guest_tls;
#endif

void **artbox_native_tls_swap(void **guest_tls) {
    void **previous = current_guest_tls;
    current_guest_tls = guest_tls;
    return previous;
}
void **artbox_bionic_get_tls(void) {
    return current_guest_tls;
}
int artbox_bionic_set_tls(void *guest_tls) {
    current_guest_tls = (void **)guest_tls;
    return 0;
}
