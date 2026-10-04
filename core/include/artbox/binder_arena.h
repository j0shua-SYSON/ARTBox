/* Original receive-arena ownership primitive. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_ARENA_H
#define ARTBOX_BINDER_ARENA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_binder_arena artbox_binder_arena;
typedef struct artbox_binder_buffer {
    uint64_t address, extent, data_size, offsets_address, offsets_size;
    uint64_t extra_address, extra_size;
} artbox_binder_buffer;

/* The owner supplies valid writable host storage and an aligned guest address
 * for the same bytes. This allocator does NOT create or register either mapping.
 * The eventual native provider must expose a separate read-only guest alias.
 * Keep both aliases alive and stop all users before destruction. Capacity is a
 * nonzero multiple of eight; metadata is allocated once, bounded by limit. */
artbox_binder_arena *artbox_binder_arena_create(void *writable, uint64_t guest_base,
    size_t capacity, size_t limit);
/* EBUSY leaves the arena intact while any buffer remains reserved/delivered. */
int artbox_binder_arena_destroy(artbox_binder_arena *arena);
/* Owner teardown only, after stopping all users: clear every live extent and
 * forget reserved/delivered buffers. The backing must remain writable. */
void artbox_binder_arena_discard_all(artbox_binder_arena *arena);
/* Best-fit allocation, eight-byte internal alignment, at least eight bytes for
 * empty transactions. Always zero reused bytes/padding. clear_on_free is 0 or 1.
 * Offset-table length must be a multiple of eight. ENOMEM means bounded bytes
 * or metadata are exhausted, not a claim about Linux's internal quota formula.
 * On failure output/state remain unchanged; no allocation occurs on this path. */
int artbox_binder_arena_reserve(artbox_binder_arena *arena, uint64_t data_size,
    uint64_t offsets_size, uint64_t extra_size, unsigned clear_on_free,
    artbox_binder_buffer *out);
/* Only the driver may write a reserved buffer, within its entire padded extent.
 * Source must be valid host storage, separate from this arena. No guest pointer
 * is accepted or followed. Publication makes the buffer immutable to this API. */
int artbox_binder_arena_write(artbox_binder_arena *arena, uint64_t address,
    uint64_t offset, const void *source, size_t size);
int artbox_binder_arena_publish(artbox_binder_arena *arena, uint64_t address);
/* Cancel is for undelivered transactions, release for delivered buffers. Both
 * require an exact allocation start; a wrong state returns EPERM. These internal
 * errors are not yet BC_FREE_BUFFER syscall error/consumption behavior. */
int artbox_binder_arena_cancel(artbox_binder_arena *arena, uint64_t address);
int artbox_binder_arena_release(artbox_binder_arena *arena, uint64_t address);
#ifdef __cplusplus
}
#endif
#endif
