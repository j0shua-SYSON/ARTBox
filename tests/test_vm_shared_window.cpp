#include "artbox/kernel.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "shared window line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static artbox_vm_ops backing;
static unsigned reservations, releases, transfers;
static bool fail_reserve, fail_reset;
static int reserve(size_t length, void** out) {
    if (fail_reserve) return -12;
    int result = backing.reserve(length, out);
    if (!result) ++reservations;
    return result;
}
static int release(void* address, size_t length) {
    ++releases;
    return backing.release(address, length);
}
static int reset(void* address, size_t length, unsigned protection) {
    int result = backing.reset(address, length, protection);
    return fail_reset ? -12 : result;
}
static int64_t fill(void*, void* buffer, size_t length) {
    ++transfers;
    std::memset(buffer, 0x5a, length);
    return static_cast<int64_t>(length);
}
static int64_t call(artbox_kernel_thread& thread, uint64_t number,
    uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0) {
    return artbox_kernel_call(&thread, number, a0, a1, a2, a3, UINT64_MAX, 0);
}
static bool outside(uint64_t address, const artbox_reference_window& window) {
    return address < window.base || address >= window.base + window.length;
}

int main() {
    backing = artbox_native_vm();
    artbox_vm_ops ops = backing;
    ops.reserve = reserve;
    ops.release = release;
    ops.reset = reset;
    const size_t page = ops.page_size;
    // Two ordinary mappings, two windows, and one borrowed range share limits.
    artbox_vm* vm = artbox_vm_create(&ops, page * 16, 5);
    artbox_system_ops system = artbox_native_system();
    artbox_kernel_thread thread;
    CHECK(vm && !artbox_kernel_thread_init(&thread, vm, &system, 100, 100));
    int64_t ordinary = call(thread, 222, 0, page, 3, 0x22);
    CHECK(ordinary > 0);
    artbox_reference_window window{17, 23, 31}, before = window, second{};
    CHECK(artbox_vm_reserve_window(nullptr, page * 8, page, &window) == -22);
    CHECK(artbox_vm_reserve_window(vm, page * 8, page, nullptr) == -22);
    CHECK(artbox_vm_reserve_window(vm, page * 8, 0, &window) == -22);
    CHECK(artbox_vm_reserve_window(vm, page * 8, page + 1, &window) == -22);
    CHECK(artbox_vm_reserve_window(vm, page * 8, page * 8, &window) == -22);
    CHECK(artbox_vm_reserve_window(vm, page * 8 + 1, page, &window) == -22);
    CHECK(artbox_vm_reserve_window(vm, UINT64_C(0x100000000) + page, page, &window) == -22);
    CHECK(artbox_vm_reserve_window(vm, page * 16, page, &window) == -12);
    fail_reserve = true;
    CHECK(artbox_vm_reserve_window(vm, page * 8, page, &window) == -12);
    fail_reserve = false;
    CHECK(!std::memcmp(&window, &before, sizeof(window)) && reservations == 1);
    CHECK(!artbox_vm_reserve_window(vm, page * 8, page, &window));
    CHECK(window.length == page * 8 && window.guard_bytes == page);
    CHECK(!artbox_vm_reserve_window(vm, page * 4, page * 2, &second));
    CHECK(artbox_vm_reserved_bytes(vm) == page * 13);
    CHECK(outside(static_cast<uint64_t>(ordinary), window));
    int64_t later = call(thread, 222, window.base + page, page, 3, 0x22);
    CHECK(later > 0 && outside(static_cast<uint64_t>(later), window));
    CHECK(outside(static_cast<uint64_t>(later), second));
    CHECK(reservations == 4 && releases == 0);
    CHECK(artbox_vm_mmap_window(nullptr, window.base, 0, page, 3, 0x22, -1, 0) == -22);
    CHECK(artbox_vm_mmap_window(vm, 0, 0, page, 3, 0x22, -1, 0) == -95);
    CHECK(artbox_vm_mmap_window(vm, static_cast<uint64_t>(ordinary), 0, page, 3, 0x22, -1, 0) == -95);
    CHECK(artbox_vm_mmap_window(vm, window.base, 0, page, 5, 0x22, -1, 0) == -1);
    // Neither the explicit allocator nor a guest MAP_FIXED can touch guards.
    CHECK(artbox_vm_mmap_window(vm, window.base, window.base, page, 3, 0x32, -1, 0) == -1);
    CHECK(call(thread, 222, second.base + page, page, 3, 0x32) == -1);
    CHECK(artbox_vm_mmap_window(vm, window.base, second.base + page * 2, page, 3, 0x32, -1, 0) == -95);
    CHECK(artbox_vm_mmap_window(vm, window.base, static_cast<uint64_t>(ordinary), page, 3, 0x32, -1, 0) == -95);
    CHECK(artbox_vm_mmap_window(vm, window.base, window.base + page * 7, page * 2, 3, 0x32, -1, 0) == -95);
    // Borrowing the reservation would incorrectly grant access to holes.
    CHECK(artbox_vm_register_data(vm, reinterpret_cast<void*>(window.base), page * 8, 3) == -17);
    CHECK(artbox_vm_register_readonly(vm, reinterpret_cast<void*>(window.base + page), page) == -17);
    CHECK(artbox_vm_transfer(vm, window.base, 8, 2, fill, nullptr) == -14);
    CHECK(artbox_vm_transfer(vm, window.base + page, 8, 2, fill, nullptr) == -14 && !transfers);

    int64_t heap = artbox_vm_mmap_window(vm, window.base, 0, page * 2, 3, 0x22, -1, 0);
    CHECK(heap == static_cast<int64_t>(window.base + page));
    uint64_t address = static_cast<uint64_t>(heap), value = 0;
    CHECK(artbox_vm_transfer(vm, address, sizeof(value), 2, fill, nullptr) == sizeof(value));
    CHECK(transfers == 1 && !artbox_vm_read(vm, address, &value, sizeof(value)));
    CHECK(value == UINT64_C(0x5a5a5a5a5a5a5a5a));
    CHECK(artbox_vm_transfer(vm, address + page * 2 - 8, 16, 2, fill, nullptr) == -14 && transfers == 1);
    CHECK(call(thread, 113, 1, address) == 0);  // Real syscall copyout into the heap.
    artbox_timespec observed{};
    CHECK(!artbox_vm_read(vm, address, &observed, sizeof(observed)));
    CHECK(observed.seconds >= 0 && observed.nanoseconds >= 0 && observed.nanoseconds < 1000000000);
    CHECK(call(thread, 226, address, page, 1) == 0);
    CHECK(call(thread, 113, 1, address) == -14);
    CHECK(artbox_vm_transfer(vm, address, 8, 2, fill, nullptr) == -14 && transfers == 1);
    CHECK(call(thread, 226, address, page, 3) == 0);
    CHECK(call(thread, 233, address, page, 4) == 0);
    CHECK(!artbox_vm_read(vm, address, &value, sizeof(value)) && value == 0);
    CHECK(call(thread, 215, address, page * 2) == 0);
    CHECK(call(thread, 113, 1, address) == -14);
    CHECK(artbox_vm_transfer(vm, address, 8, 2, fill, nullptr) == -14 && transfers == 1);
    CHECK(reservations == 4 && releases == 0 && artbox_vm_reserved_bytes(vm) == page * 14);
    CHECK(artbox_vm_mmap_window(vm, window.base, 0, page, 3, 0x22, -1, 0) == heap);
    CHECK(!artbox_vm_read(vm, address, &value, sizeof(value)) && value == 0);
    CHECK(call(thread, 222, address + page * 2, page, 3, 0x32) == static_cast<int64_t>(address + page * 2));
    CHECK(artbox_vm_mmap_window(vm, second.base, 0, page, 3, 0x22, -1, 0) == static_cast<int64_t>(second.base + page * 2));
    CHECK(call(thread, 113, 1, second.base + page * 2) == 0);

    // Borrowing storage outside the pool stays supported and consumes a slot.
    void* borrowed = nullptr;
    CHECK(!backing.reserve(page, &borrowed) && !backing.protect(borrowed, page, 3));
    CHECK(!artbox_vm_register_data(vm, borrowed, page, 3));
    CHECK(call(thread, 113, 1, reinterpret_cast<uintptr_t>(borrowed)) == 0);
    CHECK(artbox_vm_reserve_window(vm, page * 2, page, &before) == -12);
    CHECK(before.base == 17 && before.length == 23 && before.guard_bytes == 31);
    CHECK(call(thread, 215, static_cast<uint64_t>(ordinary), page) == 0);
    CHECK(call(thread, 215, static_cast<uint64_t>(later), page) == 0);
    CHECK(releases == 2 && artbox_vm_reserved_bytes(vm) == page * 12);
    CHECK(call(thread, 215, window.base, window.length) == 0);
    CHECK(call(thread, 215, second.base, second.length) == 0);
    CHECK(releases == 2 && artbox_vm_reserved_bytes(vm) == page * 12);
    CHECK(!artbox_vm_destroy(vm) && reservations == releases);
    CHECK(!backing.release(borrowed, page));

    // A native heap mutation failure invalidates the shared registry, including
    // ordinary syscall buffers. No second allocator can authorize stale pages.
    vm = artbox_vm_create(&ops, page * 8, 4);
    CHECK(vm && !artbox_kernel_thread_init(&thread, vm, &system, 100, 100));
    ordinary = call(thread, 222, 0, page, 3, 0x22);
    CHECK(ordinary > 0 && !artbox_vm_reserve_window(vm, page * 4, page, &window));
    fail_reset = true;
    CHECK(artbox_vm_mmap_window(vm, window.base, 0, page, 3, 0x22, -1, 0) == -12);
    CHECK(artbox_vm_mmap_window(vm, window.base, 0, page, 3, 0x22, -1, 0) == -5);
    CHECK(call(thread, 113, 1, static_cast<uint64_t>(ordinary)) == -5);
    CHECK(call(thread, 222, 0, page, 3, 0x22) == -5);
    CHECK(!artbox_vm_access(vm, static_cast<uint64_t>(ordinary), 8, 3));
    CHECK(artbox_vm_reserve_window(vm, page * 2, page, &before) == -5);
    CHECK(before.base == 17 && before.length == 23 && before.guard_bytes == 31);
    CHECK(!artbox_vm_destroy(vm) && reservations == releases);
    std::puts("shared heap registry: syscall copyout, guard/hole rejection, window isolation, quotas, failure and retained ownership passed");
}
