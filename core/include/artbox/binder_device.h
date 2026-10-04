/* Original bounded Binder endpoint boundary. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_DEVICE_H
#define ARTBOX_BINDER_DEVICE_H
#include "artbox/vm.h"
#include "artbox/binder_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_device artbox_binder_device;
/* One private Binder context. Limits are host admission bounds, independent of
 * BINDER_SET_MAX_THREADS. Both limits are in [1, 1024]; all metadata is reserved
 * up front. No receive mappings, transaction delivery or notification yet. */
artbox_binder_device *artbox_binder_device_create(size_t endpoints, size_t threads_per_endpoint);
/* Stop callers first. EBUSY preserves a context with live endpoints. */
int artbox_binder_device_destroy(artbox_binder_device *device);
/* Each open creates independent state, even for the same PID. Tokens are not
 * native/guest FDs and are never reused during this context's lifetime.
 * The trusted owner supplies a virtual PID/UID and keeps VM alive until close
 * and all calls finish. The initial boundary has fixed endpoint credentials. */
int artbox_binder_device_open(artbox_binder_device *device, artbox_vm *vm,
    int32_t pid, uint32_t uid, uint64_t *token);
int artbox_binder_device_close(artbox_binder_device *device, uint64_t token);
/* Negative Linux errno. Guest copies go through VM, never raw dereferences.
 * VERSION, MAX_THREADS, CONTEXT_MGR[_EXT], THREAD_EXIT and WRITE_READ's
 * ENTER/REGISTER/EXIT_LOOPER commands are implemented. Reads/transactions and
 * recognized other commands return EOPNOTSUPP; unknown words return EINVAL.
 * Write batches above 64 KiB return E2BIG without consuming commands.
 * Lock order is device then VM; VM callbacks must not enter this device. */
int64_t artbox_binder_device_ioctl(artbox_binder_device *device, uint64_t token,
    int32_t tid, uint32_t request, uint64_t argument);
#ifdef __cplusplus
}
#endif
#endif
