/* Original compile-time UAPI comparison and independent byte producer. MIT. */
#include <linux/android/binder.h>
#include <stddef.h>
#include <stdint.h>
#include "artbox/binder_wire.h"

#define SAME(name) _Static_assert(ARTBOX_##name == (uint32_t)(name), #name)
SAME(BINDER_WRITE_READ);
SAME(BINDER_SET_MAX_THREADS);
SAME(BINDER_SET_CONTEXT_MGR);
SAME(BINDER_THREAD_EXIT);
SAME(BINDER_VERSION);
SAME(BINDER_GET_NODE_DEBUG_INFO);
SAME(BINDER_GET_NODE_INFO_FOR_REF);
SAME(BINDER_SET_CONTEXT_MGR_EXT);
SAME(BINDER_FREEZE);
SAME(BINDER_GET_FROZEN_INFO);
SAME(BINDER_ENABLE_ONEWAY_SPAM_DETECTION);
SAME(BINDER_GET_EXTENDED_ERROR);
SAME(BC_TRANSACTION);
SAME(BC_REPLY);
SAME(BC_ACQUIRE_RESULT);
SAME(BC_FREE_BUFFER);
SAME(BC_INCREFS);
SAME(BC_ACQUIRE);
SAME(BC_RELEASE);
SAME(BC_DECREFS);
SAME(BC_INCREFS_DONE);
SAME(BC_ACQUIRE_DONE);
SAME(BC_ATTEMPT_ACQUIRE);
SAME(BC_REGISTER_LOOPER);
SAME(BC_ENTER_LOOPER);
SAME(BC_EXIT_LOOPER);
SAME(BC_REQUEST_DEATH_NOTIFICATION);
SAME(BC_CLEAR_DEATH_NOTIFICATION);
SAME(BC_DEAD_BINDER_DONE);
SAME(BC_TRANSACTION_SG);
SAME(BC_REPLY_SG);
SAME(BR_ERROR);
SAME(BR_OK);
SAME(BR_TRANSACTION);
SAME(BR_TRANSACTION_SEC_CTX);
SAME(BR_REPLY);
SAME(BR_ACQUIRE_RESULT);
SAME(BR_DEAD_REPLY);
SAME(BR_TRANSACTION_COMPLETE);
SAME(BR_INCREFS);
SAME(BR_ACQUIRE);
SAME(BR_RELEASE);
SAME(BR_DECREFS);
SAME(BR_ATTEMPT_ACQUIRE);
SAME(BR_NOOP);
SAME(BR_SPAWN_LOOPER);
SAME(BR_FINISHED);
SAME(BR_DEAD_BINDER);
SAME(BR_CLEAR_DEATH_NOTIFICATION_DONE);
SAME(BR_FAILED_REPLY);
SAME(BR_FROZEN_REPLY);
SAME(BR_ONEWAY_SPAM_SUSPECT);
SAME(BR_TRANSACTION_PENDING_FROZEN);
SAME(BINDER_TYPE_BINDER);
SAME(BINDER_TYPE_WEAK_BINDER);
SAME(BINDER_TYPE_HANDLE);
SAME(BINDER_TYPE_WEAK_HANDLE);
SAME(BINDER_TYPE_FD);
SAME(BINDER_TYPE_FDA);
SAME(BINDER_TYPE_PTR);
_Static_assert(ARTBOX_BINDER_PROTOCOL_VERSION == BINDER_CURRENT_PROTOCOL_VERSION, "protocol");
#define SIZE(type, size) _Static_assert(sizeof(struct type) == size, #type " size")
SIZE(binder_write_read, 48);
SIZE(binder_transaction_data, 64);
SIZE(binder_transaction_data_sg, 72);
SIZE(binder_transaction_data_secctx, 72);
SIZE(binder_handle_cookie, 12);
SIZE(binder_ptr_cookie, 16);
SIZE(binder_pri_ptr_cookie, 24);
SIZE(flat_binder_object, 24);
SIZE(binder_fd_object, 24);
SIZE(binder_buffer_object, 40);
SIZE(binder_fd_array_object, 32);
#define AT(type, member, offset) _Static_assert(offsetof(struct type, member) == offset, #type "." #member)
AT(binder_transaction_data, target, 0);
AT(binder_transaction_data, cookie, 8);
AT(binder_transaction_data, code, 16);
AT(binder_transaction_data, flags, 20);
AT(binder_transaction_data, sender_pid, 24);
AT(binder_transaction_data, sender_euid, 28);
AT(binder_transaction_data, data_size, 32);
AT(binder_transaction_data, offsets_size, 40);
AT(binder_transaction_data, data.ptr.buffer, 48);
AT(binder_transaction_data, data.ptr.offsets, 56);
AT(binder_transaction_data_sg, buffers_size, 64);
AT(binder_transaction_data_secctx, secctx, 64);
AT(binder_handle_cookie, cookie, 4);

/* Only this producer uses UAPI structs. The host reader receives the resulting
 * bytes without including this file or the NDK header. No ARM code executes. */
struct __attribute__((packed)) binder_fixture {
    uint32_t enter, send;
    struct binder_transaction_data transaction;
    uint32_t request;
    struct binder_handle_cookie death;
    uint32_t dead;
    binder_uintptr_t dead_cookie;
    struct __attribute__((packed)) {
        uint32_t prefix;
        struct flat_binder_object object;
        unsigned char padding[20];
    } parcel;
    binder_size_t offset;
};
SIZE(binder_fixture, 156);
AT(binder_fixture, transaction, 8);
AT(binder_fixture, death, 76);
AT(binder_fixture, parcel, 100);
AT(binder_fixture, offset, 148);
const struct binder_fixture artbox_binder_fixture
    __attribute__((section(".artbox.binder"), used, aligned(4))) = {
        .enter = BC_ENTER_LOOPER, .send = BC_TRANSACTION,
        .transaction = {
            .target.handle = 9, .code = 42, .flags = TF_ONE_WAY | TF_ACCEPT_FDS,
            .data_size = 24, .offsets_size = 8,
            .data.ptr = {.buffer = UINT64_C(0x1020304050607080), .offsets = UINT64_C(0x8877665544332211)},
        },
        .request = BC_REQUEST_DEATH_NOTIFICATION,
        .death = {.handle = 9, .cookie = UINT64_C(0x1122334455667788)},
        .dead = BR_DEAD_BINDER, .dead_cookie = UINT64_C(0x1122334455667788),
        .parcel.object = {.hdr.type = BINDER_TYPE_BINDER, .binder = 0x11223344, .cookie = 0x55667788},
        .offset = 4,
    };
