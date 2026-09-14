#ifndef ARTBOX_NATIVE_TLS_H
#define ARTBOX_NATIVE_TLS_H
#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed Bionic TP-relative slot pointer for the current native host thread.
 * The loader must bind initialized guest TLS before entering Bionic, and restore
 * the previous binding on return. This layer neither allocates nor frees a TCB.
 * New host threads start unbound; nested guest entries use swap/restore. */
void **artbox_native_tls_swap(void **guest_tls);

/* Precompiled pointer-only C ABI endpoints for adapted Bionic. __get_tls calls
 * the getter; its hidden __set_tls wrapper calls the setter. Host TLS and
 * reserved registers retain their native ABI ownership. A NULL pointer clears
 * the binding; invoking Bionic without initialized TLS remains a caller error. */
void **artbox_bionic_get_tls(void);
int artbox_bionic_set_tls(void *guest_tls);

#ifdef __cplusplus
}
#endif
#endif
