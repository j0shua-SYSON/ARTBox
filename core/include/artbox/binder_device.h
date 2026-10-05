/* Original bounded Binder endpoint boundary. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_DEVICE_H
#define ARTBOX_BINDER_DEVICE_H
#include "artbox/vm.h"
#include "artbox/kernel.h"
#include "artbox/binder_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_device artbox_binder_device;
/* One private Binder context. Limits are host admission bounds, independent of
 * BINDER_SET_MAX_THREADS. Both limits are in [1, 1024]; all metadata is reserved
 * up front. Receive mappings require explicit backing configuration.
 * Synchronous byte parcels and strong flat objects support one call per thread.
 * Nodes, references and death subscriptions have separate pools of 1,024;
 * 4,096 buffer-held references and 64 objects per parcel are admission bounds.
 * One-way calls have per-node ordering until receive-buffer release and do not
 * add synchronous stack entries. Weak objects, FD transfer and nesting remain
 * pending. */
artbox_binder_device *artbox_binder_device_create(size_t endpoints, size_t threads_per_endpoint);
/* Private, non-executable receive backing. create returns fresh zero-filled
 * storage of exactly length bytes, already unlinked from any guest namespace.
 * Each mapping acquires its own reference; close drops create's initial handle.
 * The context and callbacks must remain valid until device destruction. */
typedef struct artbox_binder_memory_ops {
    artbox_vm_ops memory;
    artbox_vm_file_ops mapping;
    void *context;
    int (*create)(void *context, size_t length, void **file);
    int (*close)(void *file);
} artbox_binder_memory_ops;
/* Configure once before the first open. The per-endpoint byte limit is an
 * explicit host admission bound, a native-page multiple up to 4 MiB. */
int artbox_binder_device_set_memory(artbox_binder_device *device,
    const artbox_binder_memory_ops *ops, size_t maximum_bytes);
/* Nonfixed read-only/PROT_NONE, private/shared receive views, zero offset.
 * Both sharing modes use Binder's shared bytes, with a permanent write ceiling.
 * Each open maps only once, even after unmap. Closing the endpoint retains its
 * context-manager ownership while any original receive page remains mapped.
 * Calls reap closed endpoints outside VM locks. Unused bytes are zero-backed,
 * not Linux's demand-populated SIGBUS pages; clients must not read them. */
int64_t artbox_binder_device_mmap(artbox_binder_device *device, uint64_t token,
    uint64_t address, uint64_t length, uint64_t protection, uint64_t flags, uint64_t offset);
/* Stop callers first. EBUSY preserves a context with live endpoints. */
int artbox_binder_device_destroy(artbox_binder_device *device);
/* Each open creates independent state, even for the same PID. Tokens are not
 * native/guest FDs and are never reused during this context's lifetime.
 * The trusted owner supplies a virtual PID/UID and keeps VM alive until close
 * and all calls finish. After close, a passive mapping watch is the only guest
 * VM lifetime observation; no closed endpoint dereferences its old VM pointer.
 * The initial boundary has fixed endpoint credentials. */
int artbox_binder_device_open(artbox_binder_device *device, artbox_vm *vm,
    int32_t pid, uint32_t uid, uint64_t *token);
/* Direct close returns EBUSY while an ioctl is active. VFS descriptors retain
 * their open description until the in-flight operation completes. */
int artbox_binder_device_close(artbox_binder_device *device, uint64_t token);
/* Trusted descriptor owner sets O_NONBLOCK before publishing the open. */
int artbox_binder_device_set_nonblocking(artbox_binder_device *device, uint64_t token, int enabled);
/* Negative Linux errno. Guest copies go through VM, never raw dereferences.
 * VERSION, MAX_THREADS, CONTEXT_MGR[_EXT], THREAD_EXIT and WRITE_READ's
 * ENTER/REGISTER/EXIT_LOOPER commands are implemented. Configured receive
 * contexts additionally carry BC_TRANSACTION/BC_REPLY/BC_FREE_BUFFER for the
 * documented byte/strong-object subset, reference callbacks and death/clear/ack
 * flow. Parsed malformed transactions queue BR_FAILED_REPLY and stop the write
 * batch after that transaction; invalid command/header copies still fail ioctl.
 * Unsupported operations return EOPNOTSUPP;
 * unknown words return EINVAL. New threads have one initial NOOP return;
 * subsequent empty nonblocking reads return EAGAIN. Blocking reads release the
 * device mutex until work arrives. This TID-only entry has no signal owner.
 * Write batches above 64 KiB return E2BIG without consuming commands.
 * Lock order is device then VM; VM callbacks must not enter this device. */
int64_t artbox_binder_device_ioctl(artbox_binder_device *device, uint64_t token,
    int32_t tid, uint32_t request, uint64_t argument);
/* Same dispatch with a live guest signal owner for interruptible reads. The
 * thread and its signal attachment must outlive the call. Delivered interrupt
 * epochs produce EINTR without losing consumed writes. No callback enters this
 * device from a signal handler; epoch and passive mapping checks poll at 5 ms. */
int64_t artbox_binder_device_ioctl_interruptible(artbox_binder_device *device, uint64_t token,
    artbox_kernel_thread *thread, uint32_t request, uint64_t argument);
/* Ordinary-context observation; does not enter VM or guest signal state. */
size_t artbox_binder_device_waiter_count(artbox_binder_device *device);
/* Non-consuming Linux poll readiness bits, or negative Linux errno for invalid
 * trusted arguments. May admit a new TID without clearing its initial return.
 * Thread admission exhaustion reports POLLERR. Requires configured receive
 * backing. Process readiness does not imply or require ENTER_LOOPER. No wait
 * registration is created; no work, buffer or initial return is consumed.
 * A token retained by a receive mapping remains observable after close, until
 * final unmap. This neither retains the endpoint nor dereferences its old VM.
 * VFS still rejects closed descriptors before reaching this observation. */
int artbox_binder_device_events(artbox_binder_device *device, uint64_t token, int32_t tid);
/* Subscribe a trusted ordinary-context wake hint. At most 64 listeners exist
 * per device; subscriptions have monotonic identities. Register before the
 * final readiness check to cover changes racing with sleep. Notifications may
 * coalesce and may be spurious; recheck events after waking.
 *
 * The callback runs under the device mutex. It may only publish a wake hint:
 * never reenter Binder, VFS, VM or block on guest work. It must not throw.
 * Unobserve waits for any callback to finish; after it returns, context can
 * be destroyed. No callback originates in a signal handler or passive VM
 * watch. Final-unmap detection still needs ordinary-context polling.
 * Destroy rejects a device with live listeners. Unknown subscriptions return
 * ENOENT; exhaustion returns ENOSPC. No subscription owns an endpoint. */
int artbox_binder_device_observe(artbox_binder_device *device,
    void (*notify)(void *), void *context, uint64_t *subscription);
int artbox_binder_device_unobserve(artbox_binder_device *device, uint64_t subscription);
#ifdef __cplusplus
}
#endif
#endif
