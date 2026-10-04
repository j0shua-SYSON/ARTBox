/* Original Binder boundary tests. SPDX-License-Identifier: MIT */
#include "artbox/binder_wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static void put32(unsigned char *p, uint32_t x) {
    unsigned i; for (i = 0; i < 4; ++i) p[i] = (unsigned char)(x >> (8 * i));
}
static void put64(unsigned char *p, uint64_t x) {
    unsigned i; for (i = 0; i < 8; ++i) p[i] = (unsigned char)(x >> (8 * i));
}

static void frames(void) {
    /* These are the payload lengths specified by the ARM64 Linux ABI. The
     * separate NDK producer checks constants/layout against actual UAPI types. */
    static const uint32_t writes[] = {
        ARTBOX_BC_TRANSACTION, ARTBOX_BC_REPLY, ARTBOX_BC_ACQUIRE_RESULT,
        ARTBOX_BC_FREE_BUFFER, ARTBOX_BC_INCREFS, ARTBOX_BC_ACQUIRE,
        ARTBOX_BC_RELEASE, ARTBOX_BC_DECREFS, ARTBOX_BC_INCREFS_DONE,
        ARTBOX_BC_ACQUIRE_DONE, ARTBOX_BC_ATTEMPT_ACQUIRE,
        ARTBOX_BC_REGISTER_LOOPER, ARTBOX_BC_ENTER_LOOPER, ARTBOX_BC_EXIT_LOOPER,
        ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, ARTBOX_BC_CLEAR_DEATH_NOTIFICATION,
        ARTBOX_BC_DEAD_BINDER_DONE, ARTBOX_BC_TRANSACTION_SG, ARTBOX_BC_REPLY_SG
    };
    static const unsigned lengths[] = {64,64,4,8,4,4,4,4,16,16,8,0,0,0,12,12,8,72,72};
    unsigned char storage[84];
    size_t i, n;
    for (i = 0; i < sizeof(writes) / sizeof(writes[0]); ++i) {
        unsigned char *bytes = storage + 1; /* All loads must tolerate misalignment. */
        artbox_binder_frame frame, saved;
        size_t cursor = 0;
        memset(storage, 0xab, sizeof(storage)); put32(bytes, writes[i]);
        memset(&frame, 0xcc, sizeof(frame)); memcpy(&saved, &frame, sizeof(saved));
        for (n = 1; n < 4 + lengths[i]; ++n) {
            CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, n, &cursor, &frame) == ARTBOX_BINDER_TRUNCATED);
            CHECK(cursor == 0 && !memcmp(&frame, &saved, sizeof(frame)));
        }
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, 4 + lengths[i], &cursor, &frame) == ARTBOX_BINDER_OK);
        CHECK(frame.command == writes[i] && frame.payload_size == lengths[i]);
        CHECK(frame.payload == bytes + 4 && cursor == 4 + lengths[i]);
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, cursor, &cursor, &frame) == ARTBOX_BINDER_END);
        cursor = 0;
        CHECK(artbox_binder_next(ARTBOX_BINDER_READ, bytes, 4 + lengths[i], &cursor, &frame) == ARTBOX_BINDER_UNKNOWN);
        /* Size, direction, type and ordinal are part of the command identity. */
        for (n = 8; n < 32; ++n) {
            put32(bytes, writes[i] ^ (UINT32_C(1) << n)); cursor = 0;
            CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, 80, &cursor, &frame) == ARTBOX_BINDER_UNKNOWN);
            CHECK(cursor == 0);
        }
    }
    {
        const uint32_t reads[] = {
            ARTBOX_BR_ERROR, ARTBOX_BR_OK, ARTBOX_BR_TRANSACTION,
            ARTBOX_BR_TRANSACTION_SEC_CTX, ARTBOX_BR_REPLY, ARTBOX_BR_ACQUIRE_RESULT,
            ARTBOX_BR_DEAD_REPLY, ARTBOX_BR_TRANSACTION_COMPLETE, ARTBOX_BR_INCREFS,
            ARTBOX_BR_ACQUIRE, ARTBOX_BR_RELEASE, ARTBOX_BR_DECREFS,
            ARTBOX_BR_ATTEMPT_ACQUIRE, ARTBOX_BR_NOOP, ARTBOX_BR_SPAWN_LOOPER,
            ARTBOX_BR_FINISHED, ARTBOX_BR_DEAD_BINDER, ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE,
            ARTBOX_BR_FAILED_REPLY, ARTBOX_BR_FROZEN_REPLY, ARTBOX_BR_ONEWAY_SPAM_SUSPECT,
            ARTBOX_BR_TRANSACTION_PENDING_FROZEN
        };
        const unsigned sizes[] = {4,0,64,72,64,4,0,0,16,16,16,16,24,0,0,0,8,8,0,0,0,0};
        for (i = 0; i < sizeof(reads) / sizeof(reads[0]); ++i) {
            artbox_binder_frame frame;
            size_t cursor = 0;
            put32(storage, reads[i]);
            CHECK(artbox_binder_next(ARTBOX_BINDER_READ, storage, 4 + sizes[i], &cursor, &frame) == ARTBOX_BINDER_OK);
            CHECK(frame.payload_size == sizes[i] && frame.command == reads[i]);
            cursor = 0;
            CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, storage, 4 + sizes[i], &cursor, &frame) == ARTBOX_BINDER_UNKNOWN);
        }
    }
    {
        artbox_binder_frame frame;
        size_t cursor = 0;
        put32(storage, ARTBOX_BC_ENTER_LOOPER); put32(storage + 4, 0xffffffff);
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, storage, 8, &cursor, &frame) == ARTBOX_BINDER_OK);
        CHECK(cursor == 4);
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, storage, 8, &cursor, &frame) == ARTBOX_BINDER_UNKNOWN);
        CHECK(cursor == 4); /* Earlier frame remains consumed on a later error. */
        cursor = 9;
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, storage, 8, &cursor, &frame) == ARTBOX_BINDER_INVALID);
        cursor = 0;
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, NULL, 0, &cursor, &frame) == ARTBOX_BINDER_END);
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, NULL, 1, &cursor, &frame) == ARTBOX_BINDER_INVALID);
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, storage, 8, NULL, &frame) == ARTBOX_BINDER_INVALID);
        CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, storage, 8, &cursor, NULL) == ARTBOX_BINDER_INVALID);
        CHECK(artbox_binder_next((artbox_binder_direction)2, storage, 8, &cursor, &frame) == ARTBOX_BINDER_INVALID);
    }
}

static void transactions(void) {
    unsigned char bytes[81];
    artbox_binder_frame frame;
    artbox_binder_transaction tr, saved;
    size_t cursor = 0;
    memset(bytes, 0, sizeof(bytes));
    put32(bytes + 1, ARTBOX_BC_TRANSACTION_SG);
    put64(bytes + 5, UINT64_C(0x1122334455667788));
    put64(bytes + 13, UINT64_C(0xfedcba9876543210));
    put32(bytes + 21, 123); put32(bytes + 25, 0x51);
    put32(bytes + 29, UINT32_C(0xfffffff9)); put32(bytes + 33, 10001);
    put64(bytes + 37, UINT64_MAX); put64(bytes + 45, 16);
    put64(bytes + 53, 0x104000001); put64(bytes + 61, 0x105000002);
    put64(bytes + 69, 0x106000003);
    CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes + 1, 76, &cursor, &frame) == ARTBOX_BINDER_OK);
    CHECK(artbox_binder_decode_transaction(&frame, &tr) == ARTBOX_BINDER_OK);
    CHECK(tr.target == UINT64_C(0x1122334455667788) && tr.cookie == UINT64_C(0xfedcba9876543210));
    CHECK(tr.code == 123 && tr.flags == 0x51 && tr.sender_pid == -7 && tr.sender_euid == 10001);
    CHECK(tr.data_size == UINT64_MAX && tr.offsets_size == 16);
    CHECK(tr.data_buffer == UINT64_C(0x104000001) && tr.offsets_buffer == UINT64_C(0x105000002));
    CHECK(tr.buffers_size == UINT64_C(0x106000003) && tr.security_context == 0);
    /* Decode opaque addresses; never dereference them or narrow the lengths. */
    saved = tr; frame.payload_size = 64;
    CHECK(artbox_binder_decode_transaction(&frame, &tr) == ARTBOX_BINDER_INVALID);
    CHECK(!memcmp(&tr, &saved, sizeof(tr)));
    frame.command = ARTBOX_BR_TRANSACTION_SEC_CTX; frame.payload_size = 72;
    CHECK(artbox_binder_decode_transaction(&frame, &tr) == ARTBOX_BINDER_OK);
    CHECK(tr.buffers_size == 0 && tr.security_context == UINT64_C(0x106000003));
    frame.command = ARTBOX_BC_REPLY; frame.payload_size = 64;
    CHECK(artbox_binder_decode_transaction(&frame, &tr) == ARTBOX_BINDER_OK);
    CHECK(tr.buffers_size == 0 && tr.security_context == 0);
    frame.command = ARTBOX_BR_NOOP;
    CHECK(artbox_binder_decode_transaction(&frame, &tr) == ARTBOX_BINDER_INVALID);
    CHECK(artbox_binder_decode_transaction(NULL, &tr) == ARTBOX_BINDER_INVALID);
    CHECK(artbox_binder_decode_transaction(&frame, NULL) == ARTBOX_BINDER_INVALID);
}

static void objects(void) {
    unsigned char parcel[160], offsets[25];
    size_t count = 99, n;
    const uint32_t types[] = {ARTBOX_BINDER_TYPE_BINDER, ARTBOX_BINDER_TYPE_WEAK_BINDER,
        ARTBOX_BINDER_TYPE_HANDLE, ARTBOX_BINDER_TYPE_WEAK_HANDLE, ARTBOX_BINDER_TYPE_FD,
        ARTBOX_BINDER_TYPE_PTR, ARTBOX_BINDER_TYPE_FDA};
    const size_t sizes[] = {24,24,24,24,24,40,32};
    memset(parcel, 0, sizeof(parcel));
    for (n = 0; n < sizeof(types) / sizeof(types[0]); ++n) {
        size_t length;
        put32(parcel + 4, types[n]); put64(offsets + 1, 4);
        for (length = 0; length < 4 + sizes[n]; ++length) {
            CHECK(artbox_binder_validate_objects(parcel, length, offsets + 1, 8, &count) == ARTBOX_BINDER_INVALID);
            CHECK(count == 99);
        }
        /* Object offsets are aligned to FOUR bytes, including 64-bit objects. */
        CHECK(artbox_binder_validate_objects(parcel, 4 + sizes[n], offsets + 1, 8, &count) == ARTBOX_BINDER_OK);
        CHECK(count == 1); count = 99;
    }
    put32(parcel + 4, ARTBOX_BINDER_TYPE_BINDER);
    put32(parcel + 28, ARTBOX_BINDER_TYPE_HANDLE);
    put32(parcel + 52, ARTBOX_BINDER_TYPE_PTR);
    put64(offsets + 1, 4); put64(offsets + 9, 28); put64(offsets + 17, 52);
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 24, &count) == ARTBOX_BINDER_OK);
    CHECK(count == 3); count = 99;
    for (n = 1; n < 24; ++n) {
        if (n % 8 == 0) continue;
        CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, n, &count) == ARTBOX_BINDER_INVALID);
        CHECK(count == 99);
    }
    put64(offsets + 9, 4); /* Duplicate */
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 24, &count) == ARTBOX_BINDER_INVALID);
    put64(offsets + 9, 24); /* Overlap */
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 24, &count) == ARTBOX_BINDER_INVALID);
    put64(offsets + 9, 28); put64(offsets + 17, 4); /* Reversed */
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 24, &count) == ARTBOX_BINDER_INVALID);
    put64(offsets + 1, UINT64_MAX);
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 8, &count) == ARTBOX_BINDER_INVALID);
    put64(offsets + 1, 5);
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 8, &count) == ARTBOX_BINDER_INVALID);
    put64(offsets + 1, 4); put32(parcel + 4, 0xffffffff);
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), offsets + 1, 8, &count) == ARTBOX_BINDER_UNKNOWN);
    CHECK(count == 99);
    CHECK(artbox_binder_validate_objects(NULL, 0, NULL, 0, &count) == ARTBOX_BINDER_OK && count == 0);
    CHECK(artbox_binder_validate_objects(NULL, 1, NULL, 0, &count) == ARTBOX_BINDER_INVALID);
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), NULL, 8, &count) == ARTBOX_BINDER_INVALID);
    CHECK(artbox_binder_validate_objects(parcel, sizeof(parcel), NULL, 0, NULL) == ARTBOX_BINDER_INVALID);
}

static void ndk_fixture(const char *path) {
    unsigned char bytes[161];
    FILE *input = fopen(path, "rb");
    artbox_binder_frame frame;
    artbox_binder_transaction tr;
    size_t cursor = 0, count, length;
    CHECK(input != NULL);
    length = fread(bytes, 1, sizeof(bytes), input);
    CHECK(!ferror(input) && feof(input) && fclose(input) == 0);
    CHECK(length == 156);
    CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, 88, &cursor, &frame) == ARTBOX_BINDER_OK);
    CHECK(frame.command == ARTBOX_BC_ENTER_LOOPER && cursor == 4);
    CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, 88, &cursor, &frame) == ARTBOX_BINDER_OK);
    CHECK(frame.command == ARTBOX_BC_TRANSACTION && cursor == 72);
    CHECK(artbox_binder_decode_transaction(&frame, &tr) == ARTBOX_BINDER_OK);
    CHECK(tr.target == 9 && tr.cookie == 0 && tr.code == 42 && tr.flags == 0x11);
    CHECK(tr.sender_pid == 0 && tr.sender_euid == 0 && tr.data_size == 24 && tr.offsets_size == 8);
    CHECK(tr.data_buffer == UINT64_C(0x1020304050607080) && tr.offsets_buffer == UINT64_C(0x8877665544332211));
    CHECK(artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, 88, &cursor, &frame) == ARTBOX_BINDER_OK);
    CHECK(frame.command == ARTBOX_BC_REQUEST_DEATH_NOTIFICATION && frame.payload_size == 12 && cursor == 88);
    CHECK(frame.payload[4] == 0x88 && frame.payload[11] == 0x11); /* Packed handle/cookie. */
    cursor = 88;
    CHECK(artbox_binder_next(ARTBOX_BINDER_READ, bytes, 100, &cursor, &frame) == ARTBOX_BINDER_OK);
    CHECK(frame.command == ARTBOX_BR_DEAD_BINDER && cursor == 100);
    CHECK(artbox_binder_validate_objects(bytes + 100, 48, bytes + 148, 8, &count) == ARTBOX_BINDER_OK);
    CHECK(count == 1);
    /* The object begins at offset four inside the NDK-produced parcel. */
    CHECK(bytes[148] == 4);
    puts("NDK ARM64 UAPI fixture: transaction, packed death notification and object offsets verified");
}

int main(int argc, char **argv) {
    CHECK(argc == 1 || argc == 2);
    frames(); transactions(); objects();
    if (argc == 2) ndk_fixture(argv[1]);
    puts("Binder wire: framing, full-width transactions and object boundaries verified; no driver execution");
    return 0;
}
