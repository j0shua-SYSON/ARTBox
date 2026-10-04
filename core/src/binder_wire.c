/* Original Binder stream/snapshot validation. SPDX-License-Identifier: MIT */
#include "artbox/binder_wire.h"

static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t read64(const unsigned char *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4) << 32);
}
static int write_length(uint32_t command) {
    switch (command) {
    case ARTBOX_BC_TRANSACTION: case ARTBOX_BC_REPLY: return 64;
    case ARTBOX_BC_TRANSACTION_SG: case ARTBOX_BC_REPLY_SG: return 72;
    case ARTBOX_BC_ACQUIRE_RESULT: case ARTBOX_BC_INCREFS: case ARTBOX_BC_ACQUIRE:
    case ARTBOX_BC_RELEASE: case ARTBOX_BC_DECREFS: return 4;
    case ARTBOX_BC_FREE_BUFFER: case ARTBOX_BC_ATTEMPT_ACQUIRE:
    case ARTBOX_BC_DEAD_BINDER_DONE: return 8;
    case ARTBOX_BC_INCREFS_DONE: case ARTBOX_BC_ACQUIRE_DONE: return 16;
    case ARTBOX_BC_REGISTER_LOOPER: case ARTBOX_BC_ENTER_LOOPER: case ARTBOX_BC_EXIT_LOOPER: return 0;
    case ARTBOX_BC_REQUEST_DEATH_NOTIFICATION: case ARTBOX_BC_CLEAR_DEATH_NOTIFICATION: return 12;
    default: return -1;
    }
}
static int read_length(uint32_t command) {
    switch (command) {
    case ARTBOX_BR_ERROR: case ARTBOX_BR_ACQUIRE_RESULT: return 4;
    case ARTBOX_BR_TRANSACTION: case ARTBOX_BR_REPLY: return 64;
    case ARTBOX_BR_TRANSACTION_SEC_CTX: return 72;
    case ARTBOX_BR_INCREFS: case ARTBOX_BR_ACQUIRE: case ARTBOX_BR_RELEASE:
    case ARTBOX_BR_DECREFS: return 16;
    case ARTBOX_BR_ATTEMPT_ACQUIRE: return 24;
    case ARTBOX_BR_DEAD_BINDER: case ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE: return 8;
    case ARTBOX_BR_OK: case ARTBOX_BR_DEAD_REPLY: case ARTBOX_BR_TRANSACTION_COMPLETE:
    case ARTBOX_BR_NOOP: case ARTBOX_BR_SPAWN_LOOPER: case ARTBOX_BR_FINISHED:
    case ARTBOX_BR_FAILED_REPLY: case ARTBOX_BR_FROZEN_REPLY:
    case ARTBOX_BR_ONEWAY_SPAM_SUSPECT: case ARTBOX_BR_TRANSACTION_PENDING_FROZEN: return 0;
    default: return -1;
    }
}
artbox_binder_result artbox_binder_next(artbox_binder_direction direction,
    const void *bytes, size_t size, size_t *cursor, artbox_binder_frame *out) {
    const unsigned char *p;
    artbox_binder_frame frame;
    size_t remaining;
    int length;
    if (!cursor || !out || (!bytes && size) || *cursor > size ||
        (direction != ARTBOX_BINDER_WRITE && direction != ARTBOX_BINDER_READ)) return ARTBOX_BINDER_INVALID;
    remaining = size - *cursor;
    if (!remaining) return ARTBOX_BINDER_END;
    if (remaining < 4) return ARTBOX_BINDER_TRUNCATED;
    p = (const unsigned char *)bytes + *cursor;
    frame.command = read32(p);
    length = direction == ARTBOX_BINDER_WRITE ? write_length(frame.command) : read_length(frame.command);
    if (length < 0) return ARTBOX_BINDER_UNKNOWN;
    frame.payload_size = (size_t)length;
    if (frame.payload_size > remaining - 4) return ARTBOX_BINDER_TRUNCATED;
    frame.payload = p + 4;
    *out = frame;
    *cursor += 4 + frame.payload_size;
    return ARTBOX_BINDER_OK;
}
artbox_binder_result artbox_binder_decode_transaction(
    const artbox_binder_frame *frame, artbox_binder_transaction *out) {
    const unsigned char *p;
    artbox_binder_transaction value;
    size_t length;
    if (!frame || !out || !frame->payload) return ARTBOX_BINDER_INVALID;
    switch (frame->command) {
    case ARTBOX_BC_TRANSACTION: case ARTBOX_BC_REPLY:
    case ARTBOX_BR_TRANSACTION: case ARTBOX_BR_REPLY: length = 64; break;
    case ARTBOX_BC_TRANSACTION_SG: case ARTBOX_BC_REPLY_SG:
    case ARTBOX_BR_TRANSACTION_SEC_CTX: length = 72; break;
    default: return ARTBOX_BINDER_INVALID;
    }
    if (frame->payload_size != length) return ARTBOX_BINDER_INVALID;
    p = frame->payload;
    value.target = read64(p); value.cookie = read64(p + 8);
    value.code = read32(p + 16); value.flags = read32(p + 20);
    value.sender_pid = (int32_t)read32(p + 24); value.sender_euid = read32(p + 28);
    value.data_size = read64(p + 32); value.offsets_size = read64(p + 40);
    value.data_buffer = read64(p + 48); value.offsets_buffer = read64(p + 56);
    value.buffers_size = value.security_context = 0;
    if (frame->command == ARTBOX_BR_TRANSACTION_SEC_CTX) value.security_context = read64(p + 64);
    else if (length == 72) value.buffers_size = read64(p + 64);
    *out = value;
    return ARTBOX_BINDER_OK;
}
artbox_binder_result artbox_binder_validate_objects(const void *data, size_t size,
    const void *offsets, size_t offsets_size, size_t *count) {
    const unsigned char *table = (const unsigned char *)offsets;
    size_t i, end = 0;
    if (!count || (!data && size) || (!offsets && offsets_size) || offsets_size % 8) return ARTBOX_BINDER_INVALID;
    for (i = 0; i < offsets_size; i += 8) {
        uint64_t offset = read64(table + i);
        size_t length, at;
        if (offset > size || offset % 4 || offset < end) return ARTBOX_BINDER_INVALID;
        at = (size_t)offset;
        if (size - at < 4) return ARTBOX_BINDER_INVALID;
        switch (read32((const unsigned char *)data + at)) {
        case ARTBOX_BINDER_TYPE_BINDER: case ARTBOX_BINDER_TYPE_WEAK_BINDER:
        case ARTBOX_BINDER_TYPE_HANDLE: case ARTBOX_BINDER_TYPE_WEAK_HANDLE:
        case ARTBOX_BINDER_TYPE_FD: length = 24; break;
        case ARTBOX_BINDER_TYPE_PTR: length = 40; break;
        case ARTBOX_BINDER_TYPE_FDA: length = 32; break;
        default: return ARTBOX_BINDER_UNKNOWN;
        }
        if (length > size - at) return ARTBOX_BINDER_INVALID;
        end = at + length;
    }
    *count = offsets_size / 8;
    return ARTBOX_BINDER_OK;
}
