// Original bounded allocator for Binder receive bytes. SPDX-License-Identifier: MIT
#include "artbox/binder_arena.h"
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

struct BufferSlot {
    artbox_binder_buffer value;
    bool delivered, clear;
};
struct artbox_binder_arena {
    unsigned char *writable;
    uint64_t base;
    size_t capacity, limit;
    std::mutex lock;
    std::vector<BufferSlot> buffers; // Sorted by address; fixed reserved capacity.
};

extern "C" artbox_binder_arena *artbox_binder_arena_create(void *writable,
    uint64_t guest_base, size_t capacity, size_t limit) {
    if (!writable || !guest_base || guest_base % 8 || !capacity || capacity % 8 ||
        capacity > static_cast<size_t>(PTRDIFF_MAX) || capacity > UINT64_MAX - guest_base ||
        !limit || limit > 65536) return nullptr;
    artbox_binder_arena *arena = new (std::nothrow) artbox_binder_arena;
    if (!arena) return nullptr;
    arena->writable = static_cast<unsigned char *>(writable);
    arena->base = guest_base; arena->capacity = capacity; arena->limit = limit;
    try { arena->buffers.reserve(limit); }
    catch (const std::exception&) { delete arena; return nullptr; }
    return arena;
}
extern "C" int artbox_binder_arena_destroy(artbox_binder_arena *arena) {
    if (!arena) return -22;
    { std::lock_guard<std::mutex> guard(arena->lock); if (!arena->buffers.empty()) return -16; }
    delete arena;
    return 0;
}
extern "C" int artbox_binder_arena_reserve(artbox_binder_arena *arena,
    uint64_t data_size, uint64_t offsets_size, uint64_t extra_size,
    unsigned clear_on_free, artbox_binder_buffer *out) {
    if (!arena || !out || offsets_size % 8 || clear_on_free > 1) return -22;
    std::lock_guard<std::mutex> guard(arena->lock);
    const uint64_t capacity = arena->capacity;
    if (data_size > capacity || offsets_size > capacity || extra_size > capacity ||
        arena->buffers.size() == arena->limit) return -12;
    // capacity <= PTRDIFF_MAX prevents overflow of each individual round-up.
    const uint64_t aligned_data = (data_size + 7) & ~UINT64_C(7);
    const uint64_t aligned_extra = (extra_size + 7) & ~UINT64_C(7);
    if (aligned_data > capacity || offsets_size > capacity - aligned_data) return -12;
    uint64_t extent = aligned_data + offsets_size;
    if (aligned_extra > capacity - extent) return -12;
    extent += aligned_extra;
    if (!extent) extent = 8;
    uint64_t best_size = UINT64_MAX, best_offset = 0, previous_end = 0;
    size_t best_index = 0;
    for (size_t i = 0; i <= arena->buffers.size(); ++i) {
        const uint64_t next = i == arena->buffers.size() ? capacity : arena->buffers[i].value.address - arena->base;
        const uint64_t gap = next - previous_end;
        if (gap >= extent && gap < best_size) {
            best_size = gap; best_offset = previous_end; best_index = i;
        }
        if (i != arena->buffers.size()) previous_end = next + arena->buffers[i].value.extent;
    }
    if (best_size == UINT64_MAX) return -12;
    BufferSlot slot;
    slot.value = {arena->base + best_offset, extent, data_size,
                  arena->base + best_offset + aligned_data, offsets_size,
                  arena->base + best_offset + aligned_data + offsets_size, extra_size};
    slot.delivered = false; slot.clear = clear_on_free != 0;
    // BufferSlot is trivial and reserved capacity is sufficient: no growth or
    // per-transaction allocation. Publish the output only after admission.
    arena->buffers.insert(arena->buffers.begin() + static_cast<ptrdiff_t>(best_index), slot);
    std::memset(arena->writable + static_cast<size_t>(best_offset), 0, static_cast<size_t>(extent));
    *out = slot.value;
    return 0;
}
extern "C" int artbox_binder_arena_write(artbox_binder_arena *arena,
    uint64_t address, uint64_t offset, const void *source, size_t size) {
    if (!arena || (!source && size)) return -22;
    std::lock_guard<std::mutex> guard(arena->lock);
    for (auto &slot : arena->buffers) {
        if (slot.value.address != address) continue;
        if (slot.delivered) return -1;
        if (offset > slot.value.extent || size > slot.value.extent - offset) return -22;
        if (size) std::memcpy(arena->writable + static_cast<size_t>(address - arena->base + offset), source, size);
        return 0;
    }
    return -22;
}
extern "C" int artbox_binder_arena_publish(artbox_binder_arena *arena, uint64_t address) {
    if (!arena) return -22;
    std::lock_guard<std::mutex> guard(arena->lock);
    for (auto &slot : arena->buffers) {
        if (slot.value.address != address) continue;
        if (slot.delivered) return -1;
        slot.delivered = true;
        return 0;
    }
    return -22;
}
static int remove_buffer(artbox_binder_arena *arena, uint64_t address, bool delivered) {
    if (!arena) return -22;
    std::lock_guard<std::mutex> guard(arena->lock);
    for (auto i = arena->buffers.begin(); i != arena->buffers.end(); ++i) {
        if (i->value.address != address) continue;
        if (i->delivered != delivered) return -1;
        if (i->clear) std::memset(arena->writable + static_cast<size_t>(address - arena->base),
                                0, static_cast<size_t>(i->value.extent));
        arena->buffers.erase(i);
        return 0;
    }
    return -22;
}
extern "C" int artbox_binder_arena_cancel(artbox_binder_arena *arena, uint64_t address) {
    return remove_buffer(arena, address, false);
}
extern "C" int artbox_binder_arena_release(artbox_binder_arena *arena, uint64_t address) {
    return remove_buffer(arena, address, true);
}
