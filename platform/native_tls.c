#include "artbox/native_tls.h"
#if defined(__APPLE__) && defined(__aarch64__)
#include "artbox/native_signal_binding.h"
#endif

/* Use the host compiler's TLS ABI. In particular, a Darwin build does not use
 * Android's TPIDR_EL0 slot layout or change the host's thread pointer. */
#if defined(_MSC_VER)
static __declspec(thread) void **current_guest_tls;
#else
static _Thread_local void **volatile current_guest_tls;
#endif

#if defined(__APPLE__) && defined(__aarch64__)
__attribute__((noinline))
#endif
void **artbox_native_tls_swap(void **guest_tls) {
    void **previous = current_guest_tls;
    current_guest_tls = guest_tls;
    return previous;
}
#if defined(__APPLE__) && defined(__aarch64__)
// A separate volatile read keeps Darwin compiler-TLS resolution out of the
// signal branch, including when optimization considers speculative loads.
__attribute__((noinline)) static void **ordinary_tls(void) { return current_guest_tls; }
#endif
void **artbox_bionic_get_tls(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    const artbox_native_signal_scope *scope=artbox_native_signal_current();
    if(scope) return scope->guest_tls;
    return ordinary_tls();
#else
    return current_guest_tls;
#endif
}
int artbox_bionic_set_tls(void *guest_tls) {
#if defined(__APPLE__) && defined(__aarch64__)
    if(artbox_native_signal_current()) return -95;
    (void)artbox_native_tls_swap((void **)guest_tls);
#else
    current_guest_tls = (void **)guest_tls;
#endif
    return 0;
}
