/* Original Binder descriptor contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_FILE_CHECK_H
#define ARTBOX_BINDER_FILE_CHECK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_file_ops {
    /* Flags use the Linux ARM64 ABI, including O_DIRECTORY=0x4000. */
    int (*open)(void *context, uint32_t flags);
    int (*close)(void *context, int fd);
    int64_t (*read)(void *context, int fd, uint64_t buffer, uint64_t size);
    int64_t (*write)(void *context, int fd, uint64_t buffer, uint64_t size);
    int64_t (*seek)(void *context, int fd, int64_t offset, unsigned origin);
    /* Return S_IFMT bits or negative Linux errno; normalize native stat layout. */
    int (*type)(void *context, int fd);
} artbox_binder_file_ops;
/* Four accessible bytes of scratch in the endpoint's userspace. */
int artbox_binder_file_check(void *context, const artbox_binder_file_ops *ops, void *scratch);
#ifdef __cplusplus
}
#endif
#endif
