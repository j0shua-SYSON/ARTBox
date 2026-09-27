#ifndef ARTBOX_SIGNALS_H
#define ARTBOX_SIGNALS_H
#include "artbox/kernel.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_signals artbox_signals;
/* One guest process. No host handlers or host signal masks are changed.
 * Currently implements thread-directed, blocked standard signals and waits.
 * Unblocked/default delivery and realtime queues return ENOTSUP. This is a
 * normal thread-context interface; it is not safe inside a host signal handler. */
artbox_signals *artbox_signals_create(artbox_vm *vm, int32_t pid, uint32_t uid, size_t capacity);
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
