/* Original Binder readiness expectations. SPDX-License-Identifier: MIT */
#include "check.h"
#include "artbox/binder_wire.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++cases; if (!(x)) { fprintf(stderr, "Binder readiness line %d: %s\n", __LINE__, #x); return -1; } } while (0)
#define PTR(p) ((uint64_t)(uintptr_t)(p))

int artbox_binder_poll_check(void *context, const artbox_binder_device_ops *ops,
    artbox_binder_poll_snapshot snapshot, artbox_binder_poll_scratch *scratch) {
    int cases = 0;
    memset(scratch, 0, sizeof(*scratch));
    int fd = ops->open(context); CHECK(fd >= 0);
    /* Polling allocates the thread but does not consume its initial return.
     * Binder advertises input, not regular-file writable readiness. */
    CHECK(snapshot(context, fd, 5) == 1);
    CHECK(snapshot(context, fd, 5) == 1);
    CHECK(snapshot(context, fd, 4) == 0);
    CHECK(ops->ioctl(context, fd, ARTBOX_BINDER_VERSION, PTR(&scratch->version)) == 0);
    CHECK(scratch->version == 8 && snapshot(context, fd, 5) == 0);
    /* Every ioctl exit, including an error, clears a newly allocated thread's
     * initial-return state. Thread exit itself does not allocate its successor. */
    CHECK(ops->ioctl(context, fd, ARTBOX_BINDER_THREAD_EXIT, 0) == 0);
    CHECK(snapshot(context, fd, 1) == 1);
    CHECK(ops->ioctl(context, fd, ARTBOX_BINDER_VERSION, 1) == -22);
    CHECK(snapshot(context, fd, 5) == 0);
    int64_t manager = -16;
    for (unsigned attempt = 0; attempt < 2000 && manager == -16; ++attempt) {
        manager = ops->ioctl(context, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0);
        if (manager == -16) ops->pause(context); // Earlier fixtures close asynchronously on Linux.
    }
    CHECK(manager == 0);
    /* A same-PID call to the context manager queues a thread-specific failed
     * reply. Readiness must remain asserted until its read consumes the error. */
    scratch->command[0] = ARTBOX_BC_TRANSACTION;
    scratch->transfer[0] = sizeof(scratch->command);
    scratch->transfer[2] = PTR(scratch->command);
    CHECK(ops->ioctl(context, fd, ARTBOX_BINDER_WRITE_READ, PTR(scratch->transfer)) == 0);
    CHECK(scratch->transfer[1] == sizeof(scratch->command));
    CHECK(snapshot(context, fd, 5) == 1);
    CHECK(snapshot(context, fd, 1) == 1);
    memset(scratch->transfer, 0, sizeof(scratch->transfer));
    scratch->transfer[3] = sizeof(scratch->read);
    scratch->transfer[5] = PTR(scratch->read);
    CHECK(ops->ioctl(context, fd, ARTBOX_BINDER_WRITE_READ, PTR(scratch->transfer)) == 0);
    CHECK(scratch->transfer[4] == 8 && scratch->read[0] == ARTBOX_BR_NOOP &&
          scratch->read[1] == ARTBOX_BR_FAILED_REPLY);
    CHECK(snapshot(context, fd, 5) == 0);
    int observer = ops->open(context); CHECK(observer >= 0 && observer != fd);
    CHECK(ops->ioctl(context, observer, ARTBOX_BINDER_VERSION, PTR(&scratch->version)) == 0);
    const uint64_t cookie = UINT64_C(0xa17b0123456789ab);
    memset(scratch->command, 0, sizeof(scratch->command));
    scratch->command[0] = ARTBOX_BC_ACQUIRE;
    scratch->command[2] = ARTBOX_BC_REQUEST_DEATH_NOTIFICATION;
    memcpy(scratch->command + 4, &cookie, 8);
    memset(scratch->transfer, 0, sizeof(scratch->transfer));
    scratch->transfer[0] = 24; scratch->transfer[2] = PTR(scratch->command);
    CHECK(ops->ioctl(context, observer, ARTBOX_BINDER_WRITE_READ, PTR(scratch->transfer)) == 0);
    CHECK(scratch->transfer[1] == 24);
    CHECK(snapshot(context, observer, 5) == 0);
    CHECK(ops->close(context, fd) == 0);
    /* Process readiness does not require ENTER_LOOPER. Poll must neither eat
     * the death notification nor silently register a read looper for it. */
    int ready = 0;
    for (unsigned attempt = 0; attempt < 2000 && !ready; ++attempt) {
        ready = snapshot(context, observer, 5);
        if (!ready) ops->pause(context);
    }
    CHECK(ready == 1);
    CHECK(snapshot(context, observer, 5) == 1);
    scratch->command[0] = ARTBOX_BC_ENTER_LOOPER;
    scratch->transfer[0] = 4; scratch->transfer[1] = 0;
    CHECK(ops->ioctl(context, observer, ARTBOX_BINDER_WRITE_READ, PTR(scratch->transfer)) == 0);
    memset(scratch->transfer, 0, sizeof(scratch->transfer));
    scratch->transfer[3] = sizeof(scratch->read); scratch->transfer[5] = PTR(scratch->read);
    CHECK(ops->ioctl(context, observer, ARTBOX_BINDER_WRITE_READ, PTR(scratch->transfer)) == 0);
    uint64_t delivered = 0; memcpy(&delivered, scratch->read + 2, 8);
    CHECK(scratch->transfer[4] == 16 && scratch->read[0] == ARTBOX_BR_NOOP &&
          scratch->read[1] == ARTBOX_BR_DEAD_BINDER && delivered == cookie);
    CHECK(snapshot(context, observer, 5) == 0);
    CHECK(ops->close(context, observer) == 0);
    return cases;
}
