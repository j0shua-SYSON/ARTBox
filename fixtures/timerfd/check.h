/* Original monotonic timer contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_TIMERFD_CHECK_H
#define ARTBOX_TIMERFD_CHECK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_timer_spec {
    int64_t interval_seconds, interval_nanoseconds, value_seconds, value_nanoseconds;
} artbox_timer_spec;
/* Return negative Linux errno. NULL input/output is inaccessible, except NULL
 * old_value means omit that output; pointer 1 requests inaccessible old_value.
 * Read buffers otherwise contain sixteen caller-owned bytes. Snapshot never
 * consumes expirations. monotonic_ns and pause are test clock/wait callbacks. */
typedef struct artbox_timerfd_ops {
    int (*create)(void *, uint64_t clock, uint64_t flags);
    int (*close)(void *, int fd);
    int (*settime)(void *, int fd, unsigned flags, const artbox_timer_spec *, artbox_timer_spec *old_value);
    int (*gettime)(void *, int fd, artbox_timer_spec *);
    int64_t (*read)(void *, int fd, void *, size_t);
    int64_t (*write)(void *, int fd, const void *, size_t);
    int (*snapshot)(void *, int fd, unsigned events);
    int64_t (*seek)(void *, int fd, int64_t offset, unsigned origin);
    int64_t (*monotonic_ns)(void *);
    void (*pause)(void *);
} artbox_timerfd_ops;
int artbox_timerfd_check(void *, const artbox_timerfd_ops *);
#ifdef __cplusplus
}
#endif
#endif
