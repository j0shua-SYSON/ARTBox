#ifndef ARTBOX_THREADS_H
#define ARTBOX_THREADS_H
#include "artbox/futex.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-width descriptor produced by code compiled against Bionic's headers.
 * Only the pthread clone flag set is supported, not fork/vfork/raw clone. */
#define ARTBOX_PTHREAD_CLONE_FLAGS UINT64_C(0x3d0f00)
typedef struct artbox_thread_start {
    uint64_t flags, stack_base, stack_size, tls, parent_tid, child_tid, entry, argument;
} artbox_thread_start;
typedef struct artbox_thread_finish {
    uint64_t unmap_address, unmap_size;
    int error;
} artbox_thread_finish;
typedef struct artbox_thread_ops {
    /* start returns without waiting for the entry; join waits for actual native
     * termination, including host TLS cleanup. Errors are negative Linux errno.
     * A failed start must leave no native worker alive. Callbacks cannot throw. */
    int (*start)(void *stack, size_t size, void (*entry)(void *), void *argument, void **handle);
    int (*join)(void *handle);
} artbox_thread_ops;
typedef void (*artbox_thread_run)(void *context, artbox_kernel_thread *thread,
                                  const artbox_thread_start *start, artbox_thread_finish *finish);
typedef struct artbox_threads artbox_threads;
artbox_threads *artbox_threads_create(artbox_vm *vm, artbox_futex *futex,
    const artbox_atomic_u32_ops *atomic, const artbox_system_ops *system,
    const artbox_thread_ops *native, int32_t pid, int32_t first_tid, size_t limit,
    artbox_thread_run run, void *context);
/* The run callback must return normally through its host root frame. Guest
 * final exit may use a C setjmp boundary after Bionic has run its destructors.
 * The owner keeps all interfaces/context alive until successful destruction. */
/* Called on the parent guest thread; its PID/VM must belong to this manager. */
int64_t artbox_threads_start(artbox_threads *threads, const artbox_kernel_thread *parent,
                            const artbox_thread_start *start);
/* Stop calling start before drain/destroy. A live manager returns EBUSY from
 * destroy. No detach/kill of host workers and no forced stack release. */
int artbox_threads_drain(artbox_threads *threads, uint32_t timeout_ms);
int artbox_threads_destroy(artbox_threads *threads);
size_t artbox_threads_active(artbox_threads *threads);
uint64_t artbox_threads_reaped(artbox_threads *threads);
#ifdef __cplusplus
}
#endif
#endif
