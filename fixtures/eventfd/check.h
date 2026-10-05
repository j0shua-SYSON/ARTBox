/* Original event counter contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_EVENTFD_CHECK_H
#define ARTBOX_EVENTFD_CHECK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Return negative Linux errno. Transfer pointers are either a caller-owned
 * 16-byte buffer or NULL (an inaccessible address). An adapter must preserve
 * bytes beyond the returned count. Snapshot returns filtered Linux poll bits
 * without consuming the counter. The fixture only opens nonblocking files. */
typedef struct artbox_eventfd_ops {
    int (*create)(void *, uint64_t initial, uint64_t flags);
    int (*close)(void *, int fd);
    int64_t (*read)(void *, int fd, void *, size_t);
    int64_t (*write)(void *, int fd, const void *, size_t);
    int (*snapshot)(void *, int fd, unsigned events);
    int64_t (*seek)(void *, int fd, int64_t offset, unsigned origin);
} artbox_eventfd_ops;
int artbox_eventfd_check(void *context, const artbox_eventfd_ops *ops);
#ifdef __cplusplus
}
#endif
#endif
