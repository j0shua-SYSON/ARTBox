/* Original paired native-driver contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_DEVICE_CHECK_H
#define ARTBOX_BINDER_DEVICE_CHECK_H
#include <stdint.h>
typedef struct artbox_binder_device_ops {
    int (*open)(void *context);
    int (*close)(void *context, int descriptor);
    int64_t (*ioctl)(void *context, int descriptor, uint32_t request, uint64_t argument);
    void (*pause)(void *context);
} artbox_binder_device_ops;
/* Requires a fresh private Binder context, protocol 8 and little-endian LP64.
 * Callbacks return negative Linux errno. Returns assertion count or -1. */
int artbox_binder_device_check(void *context, const artbox_binder_device_ops *ops);
#endif
