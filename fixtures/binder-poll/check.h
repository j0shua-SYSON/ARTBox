/* Original paired Binder readiness contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_POLL_CHECK_H
#define ARTBOX_BINDER_POLL_CHECK_H
#include "../binder-device/check.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_poll_scratch {
    uint64_t transfer[6];
    uint32_t version, command[17], read[32];
} artbox_binder_poll_scratch;
/* Snapshot returns Linux poll revents, or negative Linux errno. Callbacks run
 * on one thread, on nonblocking opens in a fresh private Binder context.
 * Keep scratch in caller-owned guest-visible memory. No host FD is exposed. */
typedef int (*artbox_binder_poll_snapshot)(void *context, int descriptor, unsigned events);
int artbox_binder_poll_check(void *context, const artbox_binder_device_ops *ops,
    artbox_binder_poll_snapshot snapshot, artbox_binder_poll_scratch *scratch);
#ifdef __cplusplus
}
#endif
#endif
