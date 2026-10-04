#ifndef ARTBOX_SIGNALS_H
#define ARTBOX_SIGNALS_H
#include "artbox/kernel.h"
#include "artbox/signal_actions.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_signals artbox_signals;
/* One guest process. No host handlers or host signal masks are changed.
 * Implements blocked standard signals, waits and an optional bounded realtime
 * interruption queue with a platform delivery owner. Other unblocked/default
 * delivery returns ENOTSUP. Except for explicitly identified snapshot/mask/take
 * operations, this interface requires ordinary thread context. */
artbox_signals *artbox_signals_create(artbox_vm *vm, int32_t pid, uint32_t uid, size_t capacity);
/* Enable registration only when a delivery owner exists. Configure once before
 * attaching threads. The normal-context validator checks supported dispositions,
 * flags and signed code addresses; it must not modify process state. Published
 * actions and validator context remain alive until the process is destroyed.
 * supported_flags advertises the delivery owner's capabilities. An action with
 * SA_UNSUPPORTED (0x400) probes these flags: clear unsupported bits before
 * validation/publication. Without that bit reject unsupported flags. The probe
 * bit itself cannot be advertised as supported. */
typedef int (*artbox_signal_action_validator)(void *, unsigned, const artbox_signal_action *);
int artbox_signals_enable_actions(artbox_signals *signals, size_t capacity,
    artbox_signal_action_validator validate, void *context, uint64_t supported_flags);
/* Borrow in normal context, before installing any native handler. */
artbox_signal_actions *artbox_signals_action_table(artbox_signals *signals);
/* Signal-context read; the thread must remain attached throughout delivery. */
int artbox_signals_mask_snapshot(const artbox_kernel_thread *thread, uint64_t *mask);
/* Capability of this compiled target: mask/pending transitions must be lock-free
 * before a delivery owner may change masks inside a host handler. */
int artbox_signals_handler_mask_support(void);
/* Signal-context operation on a live descriptor; buffers are trusted host data.
 * BLOCK=0, UNBLOCK=1, SETMASK=2. Null input only queries, ignoring how. Preserve
 * output on error. Pending signals that would become unblocked return ENOTSUP
 * without mutation until their delivery transport exists. No VM access/locks. */
int artbox_signals_mask_update(artbox_kernel_thread *thread, uint32_t how,
    const uint64_t *input, uint64_t *previous);
typedef struct artbox_signal_stack { uint64_t address,size; uint32_t flags; } artbox_signal_stack;
/* Enable only with an actual platform stack-switching delivery owner, before
 * attaching threads. Minimum size is part of the runtime's advertised ABI.
 * Records are immutable and retained until thread detach; capacity exhaustion
 * returns ENOMEM. Clone with shared VM starts with a disabled alternate stack. */
int artbox_signals_enable_stacks(artbox_signals *signals, size_t minimum, size_t capacity);
/* Snapshot is signal-safe on a live attached descriptor. The stack pointer is
 * the guest execution SP; SS_ONSTACK is derived from it, not from host flags. */
int artbox_signals_stack_snapshot(const artbox_kernel_thread *thread, uint64_t sp,
    artbox_signal_stack *output);
/* Normal context, single owning writer only. Host buffers are trusted. Require
 * owned writable guest memory, publish before syscall copyout, preserve output
 * on error. A signal handler uses snapshot and cannot call this allocating API. */
int artbox_signals_stack_update(artbox_kernel_thread *thread, uint64_t sp,
    const artbox_signal_stack *input, artbox_signal_stack *previous);
/* Caller owns each kernel descriptor until detach; attach/clone before its
 * thread starts, detach only after it stops. Destroy rejects attached threads. */
int artbox_signals_attach(artbox_signals *signals, artbox_kernel_thread *thread);
int artbox_signals_inherit(const artbox_kernel_thread *parent, artbox_kernel_thread *child);
int artbox_signals_detach(artbox_kernel_thread *thread);
int artbox_signals_destroy(artbox_signals *signals);
size_t artbox_signals_thread_count(artbox_signals *signals);
size_t artbox_signals_waiter_count(artbox_signals *signals);
/* Configure one realtime thread-interruption signal (32..64) before attachment.
 * tgkill records from this single guest process all carry the same SI_TKILL,
 * pid and uid; a bounded count preserves every accepted send without allocation.
 * Other realtime signals and user-supplied siginfo remain unsupported. */
int artbox_signals_enable_interrupt(artbox_signals *signals, unsigned number, uint32_t capacity);
unsigned artbox_signals_interrupt_number(const artbox_kernel_thread *thread);
/* Bind on the owning thread before guest execution. Unbind after blocking and
 * quiescing the native transport, before detach. The callback is nonblocking,
 * signal-safe and keeps its target alive; it may run under the process mutex or
 * from a handler's mask restoration. It must not re-enter normal-context APIs.
 * Null removes a binding. Unblocked delivery is rejected without a binding. */
typedef void (*artbox_signal_interrupt_notify)(void *);
int artbox_signals_bind_interrupt(artbox_kernel_thread *thread,
    artbox_signal_interrupt_notify notify, void *context);
/* Single owning consumer: atomically take one unblocked interruption and add
 * its self/action mask. Return its signal number, zero if unavailable, or errno.
 * The owner restores previous_mask after invoking the handler. Signal-safe only
 * where artbox_signals_handler_mask_support() is true; no VM access or allocation.
 * Other hosts may exercise the same operations in ordinary thread context. */
int artbox_signals_take_interrupt(artbox_kernel_thread *thread, uint64_t action_mask,
    uint64_t *previous_mask);
/* Advances when the owning delivery thread takes an interruption. Interruptible
 * host waits compare this epoch without calling a mutex or condition variable
 * from the native signal handler. */
uint64_t artbox_signals_interrupt_epoch(const artbox_kernel_thread *thread);
int64_t artbox_signals_call(artbox_kernel_thread *thread, uint64_t number,
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

#ifdef __cplusplus
}
#endif
#endif
