// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_SIGNAL_DELIVERY_H
#define ARTBOX_NATIVE_SIGNAL_DELIVERY_H
#include "artbox/signals.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_signal_memory_range { uint64_t address,size; } artbox_signal_memory_range;
typedef struct artbox_native_signal_thread {
    artbox_kernel_thread *kernel;
    artbox_signal_actions *actions;
    const artbox_signal_memory_range *code;
    size_t code_count;
    const artbox_signal_memory_range *data;
    size_t data_count;
    uint64_t stack_address,stack_size;
    void **guest_tls;
    void *platform_state;
} artbox_native_signal_thread;
enum { ARTBOX_SIGNAL_STACK_MINIMUM = 8192 };
/* Ordinary-context lifetime, around all guest execution on this thread. Own a
 * guarded host alternate stack for conversion, save/restore any prior host
 * registration, and attach/detach the separate syscall/TLS scope. */
int artbox_native_signal_thread_attach(artbox_native_signal_thread *thread);
int artbox_native_signal_thread_detach(artbox_native_signal_thread *thread);
/* Caller owns stable signed RX, writable data and stack ranges throughout delivery. This
 * initial bridge handles BRK/SIGTRAP, SA_SIGINFO, optional SA_RESTART/SA_ONSTACK,
 * and Linux action masks. It never installs a process-wide host handler.
 * Unsupported dispositions/flags must be rejected by the registration owner. */
int artbox_native_signal_validate_trap(void *context,unsigned number,const artbox_signal_action *action);
/* Called from a host SA_ONSTACK SIGTRAP callback on an attached thread. The
 * owner keeps registered guest stacks mapped and writable throughout delivery.
 * Captures Darwin
 * state, invokes a signed Android handler with Linux data and resumes validated
 * register edits. Returns a Linux error without editing the host context on
 * failure. Getpid/gettid, mask changes and alternate-stack queries use the
 * separate dispatcher. Buffers may use the stable image ranges or attached
 * stacks, with writes limited to RW storage. Mask changes and edited return
 * masks preserve queued signals; unsupported pending-unblock delivery fails.
 * Active alternate-stack updates fail with EPERM; other alternate-stack
 * updates and edited return-stack metadata remain unsupported.
 * Host disposition/default handling remains the caller's responsibility. */
int artbox_native_signal_deliver_trap(artbox_native_signal_thread *thread,void *host_context);
#ifdef __cplusplus
}
#endif
#endif
