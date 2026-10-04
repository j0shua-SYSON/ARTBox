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
    unsigned interrupt_signal;
} artbox_native_signal_thread;
enum { ARTBOX_SIGNAL_STACK_MINIMUM = 8192 };
/* Ordinary-context lifetime, around all guest execution on this thread. Own a
 * guarded host alternate stack for conversion, save/restore any prior host
 * registration, and attach/detach the separate syscall/TLS scope. */
int artbox_native_signal_thread_attach(artbox_native_signal_thread *thread);
int artbox_native_signal_thread_detach(artbox_native_signal_thread *thread);
/* Caller owns stable signed RX, writable data and stack ranges throughout delivery. This
 * bridge handles BRK/SIGTRAP, measured memory SIGSEGV/SIGBUS and UDF/SIGILL,
 * SA_SIGINFO, optional SA_RESTART/SA_ONSTACK, and Linux action masks. It never
 * installs a process-wide host handler.
 * Unsupported dispositions/flags must be rejected by the registration owner. */
int artbox_native_signal_validate_fault(void *context,unsigned number,const artbox_signal_action *action);
/* Also permit a one-argument, non-restarting handler for interrupt_signal.
 * Its process owner installs SIGUSR1 with SA_SIGINFO|SA_ONSTACK, no SA_RESTART.
 * SIGUSR1 is exclusively owned by this transport during guest execution. */
int artbox_native_signal_validate_action(void *context,unsigned number,const artbox_signal_action *action);
/* Public pthread notification can interrupt either signed guest code or a host
 * syscall on its registered stack. Invoke the one-argument signed callback,
 * preserve the host context, and drain the portable realtime count. No Linux
 * ucontext editing, SA_SIGINFO/SA_RESTART or nonlocal exits on this path. */
int artbox_native_signal_deliver_interrupt(artbox_native_signal_thread *thread,void *host_context);
/* Called from a host SA_SIGINFO|SA_ONSTACK fault callback on an attached thread. The
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
 * Data faults use a nonblocking VM metadata query: a concurrent mapping change
 * returns EAGAIN; file EOF/I/O faults, MTE and other unmeasured cases remain
 * unsupported. Nested synchronous faults are not supported by this owner.
 * host_info points to native siginfo_t, never the Android wire structure.
 * Host disposition/default handling remains the caller's responsibility. */
int artbox_native_signal_deliver_fault(artbox_native_signal_thread *thread,int host_number,
    const void *host_info,void *host_context);
#ifdef __cplusplus
}
#endif
#endif
