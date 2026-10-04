/* Original Binder receive-mapping contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_MAPPING_CHECK_H
#define ARTBOX_BINDER_MAPPING_CHECK_H
#include "../binder-device/check.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_mapping_ops {
    uint64_t page_size;
    int64_t (*map)(void *context, int fd, uint64_t length, unsigned protection,
                   unsigned flags, uint64_t offset);
    int (*protect)(void *context, uint64_t address, uint64_t length, unsigned protection);
    int (*unmap)(void *context, uint64_t address, uint64_t length);
} artbox_binder_mapping_ops;
/* Linux protection/sharing bits; no executable mappings. Never read unused
 * receive bytes: Linux has not populated those pages with transaction data.
 * Requires the same context-manager UID as earlier fixtures in this context. */
int artbox_binder_mapping_check(void *context, const artbox_binder_device_ops *device,
    const artbox_binder_mapping_ops *memory);
#ifdef __cplusplus
}
#endif
#endif
