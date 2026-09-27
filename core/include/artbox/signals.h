#ifndef ARTBOX_SIGNALS_H
#define ARTBOX_SIGNALS_H
#include "artbox/kernel.h"
#include "artbox/signal_actions.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_signals artbox_signals;
/* One guest process. No host handlers or host signal masks are changed.
 * Currently implements thread-directed, blocked standard signals and waits.
 * Unblocked/default delivery and realtime queues return ENOTSUP. This is a
 * normal thread-context interface; it is not safe inside a host signal handler. */
artbox_signals *artbox_signals_create(artbox_vm *vm, int32_t pid, uint32_t uid, size_t capacity);
/* Enable registration only when a delivery owner exists. Configure once before
 * attaching threads. The normal-context validator checks supported dispositions,
 * flags and signed code addresses; it must not modify process state. Published
 * actions and validator context remain alive until the process is destroyed. */
typedef int (*artbox_signal_action_validator)(void *, unsigned, const artbox_signal_action *);
int artbox_signals_enable_actions(artbox_signals *signals, size_t capacity,
    artbox_signal_action_validator validate, void *context);
/* Borrow in normal context, before installing any native handler. */
artbox_signal_actions *artbox_signals_action_table(artbox_signals *signals);
/* Signal-context read; the thread must remain attached throughout delivery. */
int artbox_signals_mask_snapshot(const artbox_kernel_thread *thread, uint64_t *mask);
/* Caller owns each kernel descriptor until detach; attach/clone before its
 * thread starts, detach only after it stops. Destroy rejects attached threads. */
int artbox_signals_attach(artbox_signals *signals, artbox_kernel_thread *thread);
int artbox_signals_inherit(const artbox_kernel_thread *parent, artbox_kernel_thread *child);
int artbox_signals_detach(artbox_kernel_thread *thread);
int artbox_signals_destroy(artbox_signals *signals);
size_t artbox_signals_thread_count(artbox_signals *signals);
size_t artbox_signals_waiter_count(artbox_signals *signals);
int64_t artbox_signals_call(artbox_kernel_thread *thread, uint64_t number,
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

#ifdef __cplusplus
}
#endif
#endif
