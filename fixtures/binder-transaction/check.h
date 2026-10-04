/* Original threaded Binder transaction contract. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_TRANSACTION_CHECK_H
#define ARTBOX_BINDER_TRANSACTION_CHECK_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_transaction_scratch {
    uint64_t transfer[6];
    unsigned char write[128], read[256], data[32];
} artbox_binder_transaction_scratch;
typedef struct artbox_binder_transaction_ops {
    int32_t (*pid)(void *context, int32_t role);
    uint32_t uid;
    size_t page_size;
    int (*open)(void *context, int32_t role);
    int (*close)(void *context, int fd);
    int64_t (*map)(void *context, int fd, size_t length);
    int (*unmap)(void *context, uint64_t address, size_t length);
    int64_t (*ioctl)(void *context, int fd, int32_t tid, uint32_t request, uint64_t argument);
    int (*read)(void *context, uint64_t address, void *out, size_t length);
    void (*pause)(void *context);
    /* Execute the two owners concurrently, then join both. The real Linux
     * reference needs a separate client process: handle-zero self-calls are
     * rejected by PID even with separate opens. ARTBox uses virtual PIDs.
     * The left/server endpoint is prepared before this call; right/client
     * opens inside its callback. No callback may outlive this function. */
    int (*parallel)(void *context, int (*left)(void *), void *left_arg,
        int (*right)(void *), void *right_arg, int results[2]);
} artbox_binder_transaction_ops;
/* Scratch ranges belong to the calling VM/native process. Each endpoint owns
 * its own scratch, mapping and TID. Uses nonblocking endpoints and bounded waits.
 * Returns zero only after ping/reply, both completions and both buffer-free
 * commands are consumed. Death/reference-object transfer is a separate check. */
int artbox_binder_transaction_check(void *context, const artbox_binder_transaction_ops *ops,
    artbox_binder_transaction_scratch *server, artbox_binder_transaction_scratch *client);
int artbox_binder_transaction_same_pid_check(void *context, const artbox_binder_transaction_ops *ops,
    artbox_binder_transaction_scratch *server, artbox_binder_transaction_scratch *client);
#ifdef __cplusplus
}
#endif
#endif
