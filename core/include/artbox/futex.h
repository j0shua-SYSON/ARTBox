#ifndef ARTBOX_FUTEX_H
#define ARTBOX_FUTEX_H
#include "artbox/kernel.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_futex artbox_futex;
/* One wait domain per guest address space. Shared/private wait and wake use
 * distinct keys in the same-address, single-process case. Plain private requeue
 * moves waiters between keys; its fourth argument is a count, not a timeout.
 * Shared requeue, comparison/PI operations, shared file aliases and robust-owner
 * recovery are not provided. The interruptible entry supports the configured
 * non-restarting signal only. */
artbox_futex *artbox_futex_create(artbox_vm *vm, const artbox_atomic_u32_ops *atomic,
                                 const artbox_system_ops *system, size_t waiter_limit);
/* The owner must stop callers before destruction. Active waits return EBUSY. */
int artbox_futex_destroy(artbox_futex *futex);
int64_t artbox_futex_call(artbox_futex *futex, uint64_t address, uint64_t operation,
                         uint64_t value, uint64_t timeout, uint64_t address2, uint64_t bitset);
/* The attached owning thread can receive the configured non-restarting signal.
 * Delivered interruptions return EINTR. A bounded ordinary-context poll avoids
 * touching a host condition variable from a native signal handler. */
int64_t artbox_futex_call_interruptible(artbox_futex *futex, artbox_kernel_thread *thread,
    uint64_t address, uint64_t operation, uint64_t value, uint64_t timeout,
    uint64_t address2, uint64_t bitset);
/* Call only after the native child has stopped using its guest stack/TCB.
 * Clear the registered word with release ordering, then wake one shared waiter. */
int artbox_futex_clear_tid(artbox_futex *futex, uint64_t address);
/* Diagnostic snapshot, not a synchronization primitive for guest programs. */
size_t artbox_futex_waiters(artbox_futex *futex, uint64_t address, int private_key);
#ifdef __cplusplus
}
#endif
#endif
