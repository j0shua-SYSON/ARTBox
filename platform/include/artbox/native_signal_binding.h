// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_SIGNAL_BINDING_H
#define ARTBOX_NATIVE_SIGNAL_BINDING_H
#include "artbox/native_syscall.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_native_signal_scope {
    artbox_syscall_binding syscall;
    void **guest_tls;
} artbox_native_signal_scope;

/* Apple arm64 only. Attach/detach run in ordinary thread context, before/after
 * any guest handler can execute. One public pthread key is retained for the
 * library lifetime; per-thread state is freed by detach. The caller owns the
 * opaque context until detach. A second attach or active-scope detach fails. */
int artbox_native_signal_attach(void *context);
int artbox_native_signal_detach(void);
/* These calls never initialize a key, allocate or lock. They are the signal
 * path for an already attached thread. Scope and its binding are immutable,
 * borrowed until restored. Nested delivery restores the previous scope. */
void *artbox_native_signal_thread_context(void);
int artbox_native_signal_scope_swap(const artbox_native_signal_scope *scope,
    const artbox_native_signal_scope **previous);
const artbox_native_signal_scope *artbox_native_signal_current(void);
#ifdef __cplusplus
}
#endif
#endif
