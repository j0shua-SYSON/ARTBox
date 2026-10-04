/* Original 64-bit little-endian Binder wire boundary. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_WIRE_H
#define ARTBOX_BINDER_WIRE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Linux generic/ARM64 ioctl numbers; never use Darwin's _IO* macros here.
 * Recognizing a frame does not imply that the driver implements its operation. */
#define ARTBOX_BINDER_PROTOCOL_VERSION 8
#define ARTBOX_BINDER_WRITE_READ UINT32_C(0xc0306201)
#define ARTBOX_BINDER_SET_MAX_THREADS UINT32_C(0x40046205)
#define ARTBOX_BINDER_SET_CONTEXT_MGR UINT32_C(0x40046207)
#define ARTBOX_BINDER_THREAD_EXIT UINT32_C(0x40046208)
#define ARTBOX_BINDER_VERSION UINT32_C(0xc0046209)
#define ARTBOX_BINDER_GET_NODE_DEBUG_INFO UINT32_C(0xc018620b)
#define ARTBOX_BINDER_GET_NODE_INFO_FOR_REF UINT32_C(0xc018620c)
#define ARTBOX_BINDER_SET_CONTEXT_MGR_EXT UINT32_C(0x4018620d)
#define ARTBOX_BINDER_FREEZE UINT32_C(0x400c620e)
#define ARTBOX_BINDER_GET_FROZEN_INFO UINT32_C(0xc00c620f)
#define ARTBOX_BINDER_ENABLE_ONEWAY_SPAM_DETECTION UINT32_C(0x40046210)
#define ARTBOX_BINDER_GET_EXTENDED_ERROR UINT32_C(0xc00c6211)

#define ARTBOX_BC_TRANSACTION UINT32_C(0x40406300)
#define ARTBOX_BC_REPLY UINT32_C(0x40406301)
#define ARTBOX_BC_ACQUIRE_RESULT UINT32_C(0x40046302)
#define ARTBOX_BC_FREE_BUFFER UINT32_C(0x40086303)
#define ARTBOX_BC_INCREFS UINT32_C(0x40046304)
#define ARTBOX_BC_ACQUIRE UINT32_C(0x40046305)
#define ARTBOX_BC_RELEASE UINT32_C(0x40046306)
#define ARTBOX_BC_DECREFS UINT32_C(0x40046307)
#define ARTBOX_BC_INCREFS_DONE UINT32_C(0x40106308)
#define ARTBOX_BC_ACQUIRE_DONE UINT32_C(0x40106309)
#define ARTBOX_BC_ATTEMPT_ACQUIRE UINT32_C(0x4008630a)
#define ARTBOX_BC_REGISTER_LOOPER UINT32_C(0x0000630b)
#define ARTBOX_BC_ENTER_LOOPER UINT32_C(0x0000630c)
#define ARTBOX_BC_EXIT_LOOPER UINT32_C(0x0000630d)
#define ARTBOX_BC_REQUEST_DEATH_NOTIFICATION UINT32_C(0x400c630e)
#define ARTBOX_BC_CLEAR_DEATH_NOTIFICATION UINT32_C(0x400c630f)
#define ARTBOX_BC_DEAD_BINDER_DONE UINT32_C(0x40086310)
#define ARTBOX_BC_TRANSACTION_SG UINT32_C(0x40486311)
#define ARTBOX_BC_REPLY_SG UINT32_C(0x40486312)

#define ARTBOX_BR_ERROR UINT32_C(0x80047200)
#define ARTBOX_BR_OK UINT32_C(0x00007201)
#define ARTBOX_BR_TRANSACTION UINT32_C(0x80407202)
#define ARTBOX_BR_TRANSACTION_SEC_CTX UINT32_C(0x80487202)
#define ARTBOX_BR_REPLY UINT32_C(0x80407203)
#define ARTBOX_BR_ACQUIRE_RESULT UINT32_C(0x80047204)
#define ARTBOX_BR_DEAD_REPLY UINT32_C(0x00007205)
#define ARTBOX_BR_TRANSACTION_COMPLETE UINT32_C(0x00007206)
#define ARTBOX_BR_INCREFS UINT32_C(0x80107207)
#define ARTBOX_BR_ACQUIRE UINT32_C(0x80107208)
#define ARTBOX_BR_RELEASE UINT32_C(0x80107209)
#define ARTBOX_BR_DECREFS UINT32_C(0x8010720a)
#define ARTBOX_BR_ATTEMPT_ACQUIRE UINT32_C(0x8018720b)
#define ARTBOX_BR_NOOP UINT32_C(0x0000720c)
#define ARTBOX_BR_SPAWN_LOOPER UINT32_C(0x0000720d)
#define ARTBOX_BR_FINISHED UINT32_C(0x0000720e)
#define ARTBOX_BR_DEAD_BINDER UINT32_C(0x8008720f)
#define ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE UINT32_C(0x80087210)
#define ARTBOX_BR_FAILED_REPLY UINT32_C(0x00007211)
#define ARTBOX_BR_FROZEN_REPLY UINT32_C(0x00007212)
#define ARTBOX_BR_ONEWAY_SPAM_SUSPECT UINT32_C(0x00007213)
#define ARTBOX_BR_TRANSACTION_PENDING_FROZEN UINT32_C(0x00007214)

#define ARTBOX_BINDER_TYPE_BINDER UINT32_C(0x73622a85)
#define ARTBOX_BINDER_TYPE_WEAK_BINDER UINT32_C(0x77622a85)
#define ARTBOX_BINDER_TYPE_HANDLE UINT32_C(0x73682a85)
#define ARTBOX_BINDER_TYPE_WEAK_HANDLE UINT32_C(0x77682a85)
#define ARTBOX_BINDER_TYPE_FD UINT32_C(0x66642a85)
#define ARTBOX_BINDER_TYPE_FDA UINT32_C(0x66646185)
#define ARTBOX_BINDER_TYPE_PTR UINT32_C(0x70742a85)

typedef enum artbox_binder_result {
    ARTBOX_BINDER_OK, ARTBOX_BINDER_END, ARTBOX_BINDER_TRUNCATED,
    ARTBOX_BINDER_UNKNOWN, ARTBOX_BINDER_INVALID
} artbox_binder_result;
typedef enum artbox_binder_direction {
    ARTBOX_BINDER_WRITE, ARTBOX_BINDER_READ
} artbox_binder_direction;
typedef struct artbox_binder_frame {
    uint32_t command;
    const unsigned char *payload;
    size_t payload_size;
} artbox_binder_frame;
/* Decoded values, NOT a native wire struct. Addresses are opaque guest values. */
typedef struct artbox_binder_transaction {
    uint64_t target, cookie;
    uint32_t code, flags;
    int32_t sender_pid;
    uint32_t sender_euid;
    uint64_t data_size, offsets_size, data_buffer, offsets_buffer;
    uint64_t buffers_size, security_context;
} artbox_binder_transaction;

/* Parse one complete frame from an immutable, already-copied command stream.
 * No allocation, pointer dereference into guest memory, or driver side effect.
 * On OK advance cursor and set a borrowed payload view. On any other result,
 * leave cursor and output unchanged. Commands match the full ioctl word;
 * their encoded size is not trusted to skip unknown commands. */
artbox_binder_result artbox_binder_next(artbox_binder_direction direction,
    const void *bytes, size_t size, size_t *cursor, artbox_binder_frame *out);
artbox_binder_result artbox_binder_decode_transaction(
    const artbox_binder_frame *frame, artbox_binder_transaction *out);
/* Check object types, extents and ordered non-overlapping offsets in an owned
 * parcel snapshot. Lengths describe accessible HOST storage, not guest pointers.
 * An object's offset is aligned to four bytes, its table entry occupies eight.
 * This is structural validation only: handle ownership, references, FDs, parent
 * fixups and flag semantics belong to transaction delivery. On failure leave
 * count unchanged. NULL is allowed only for an empty corresponding buffer. */
artbox_binder_result artbox_binder_validate_objects(const void *data, size_t size,
    const void *offsets, size_t offsets_size, size_t *count);
#ifdef __cplusplus
}
#endif
#endif
