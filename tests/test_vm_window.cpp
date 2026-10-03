#include "artbox/managed_reference.h"
#include "artbox/native_vm.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "window line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static artbox_vm_ops backing;
static std::atomic<unsigned> reservations{0}, releases{0};
static bool fail_reset, fail_reserve;
static unsigned file_calls;
static int acquire_file(void*, void**) { ++file_calls; return -5; }
static void release_file(void*) { ++file_calls; }
static int map_file(void*, void*, size_t, unsigned, unsigned, uint64_t) { ++file_calls; return -5; }
static int sync_file(void*, void*, size_t, unsigned) { ++file_calls; return -5; }
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
    return fail_reset ? -12 : result;  // Failure after a real native mutation.
}

int main() {
    backing = artbox_native_vm();
    artbox_vm_ops ops = backing;
    ops.reserve = reserve;
    ops.release = release;
    ops.reset = reset;
    const size_t page = ops.page_size;
    artbox_reference_window window{17, 23, 31}, before = window;
    CHECK(!artbox_vm_create_window(nullptr, page * 8, page, &window));
    CHECK(!artbox_vm_create_window(&ops, page * 8, 0, &window));
    CHECK(!artbox_vm_create_window(&ops, page * 8, page + 1, &window));
    CHECK(!artbox_vm_create_window(&ops, page * 8, page * 8, &window));
    CHECK(!artbox_vm_create_window(&ops, page * 8 + 1, page, &window));
    CHECK(!artbox_vm_create_window(&ops, UINT64_C(0x100000000) + page, page, &window));
    CHECK(!artbox_vm_create_window(&ops, page * 8, page, nullptr));
    CHECK(!std::memcmp(&window, &before, sizeof(window)) && reservations == 0);
    fail_reserve = true;
    CHECK(!artbox_vm_create_window(&ops, page * 8, page, &window));
    CHECK(!std::memcmp(&window, &before, sizeof(window)) && reservations == 0);
    fail_reserve = false;

    artbox_vm* vm = artbox_vm_create_window(&ops, page * 8, page, &window);
    CHECK(vm && window.length == page * 8 && window.guard_bytes == page);
    CHECK(window.base >= UINT64_C(0x100000000));
    CHECK(reservations == 1 && releases == 0 && artbox_vm_reserved_bytes(vm) == window.length);
    CHECK(!artbox_vm_access(vm, window.base, page * 8, 0));
    CHECK(artbox_vm_mmap(vm, window.base, page, 3, 0x32, -1, 0) == -1);
    CHECK(artbox_vm_mprotect(vm, window.base, page, 3) == -12);
    CHECK(artbox_vm_mmap(vm, window.base + window.length, page, 3, 0x32, -1, 0) == -95);
    CHECK(artbox_vm_mmap(vm, 0, page, 5, 0x22, -1, 0) == -1);
    CHECK(artbox_vm_register_data(vm, reinterpret_cast<void*>(window.base), page, 3) == -95);
    CHECK(artbox_vm_register_readonly(vm, reinterpret_cast<void*>(window.base), page) == -95);
    artbox_vm_file_ops file_ops{acquire_file, release_file, map_file, sync_file};
    CHECK(artbox_vm_map_file(vm, 0, page, 3, 2, 0, &file_calls, &file_ops, 3) == -95);
    CHECK(!file_calls);
    int64_t first = artbox_vm_mmap(vm, 0, page * 2, 3, 0x22, -1, 0);
    CHECK(first == static_cast<int64_t>(window.base + page));
    uint32_t reference = 0;
    uint64_t decoded = 0, value = UINT64_C(0x123456789abcdef0), readback = 0;
    CHECK(!artbox_reference_encode(&window, static_cast<uint64_t>(first), &reference) && reference == page);
    CHECK(!artbox_reference_decode(&window, reference, &decoded) && decoded == static_cast<uint64_t>(first));
    CHECK(!artbox_vm_write(vm, decoded, &value, sizeof(value)));
    CHECK(!artbox_vm_read(vm, decoded, &readback, sizeof(readback)) && readback == value);
    int64_t hinted = artbox_vm_mmap(vm, window.base + 6 * page, page, 3, 0x22, -1, 0);
    CHECK(hinted == static_cast<int64_t>(window.base + 6 * page));
    int64_t overlap = artbox_vm_mmap(vm, static_cast<uint64_t>(first), page, 3, 0x22, -1, 0);
    CHECK(overlap == static_cast<int64_t>(window.base + 3 * page));
    CHECK(!artbox_vm_read(vm, static_cast<uint64_t>(first), &readback, sizeof(readback)) && readback == value);
    CHECK(!artbox_vm_munmap(vm, static_cast<uint64_t>(first), page * 2));
    CHECK(!artbox_vm_access(vm, static_cast<uint64_t>(first), page, 0));
    CHECK(artbox_vm_mmap(vm, 0, page * 2, 3, 0x22, -1, 0) == first);
    CHECK(!artbox_vm_read(vm, static_cast<uint64_t>(first), &readback, sizeof(readback)) && readback == 0);
    CHECK(artbox_vm_mmap(vm, 0, page * 4, 3, 0x22, -1, 0) == -12);  // Fragmented holes.
    CHECK(!artbox_vm_munmap(vm, window.base, window.length));
    CHECK(reservations == 1 && releases == 0 && artbox_vm_reserved_bytes(vm) == window.length);
    CHECK(artbox_vm_mmap(vm, 0, page * 7, 3, 0x22, -1, 0) == first);
    CHECK(artbox_vm_mmap(vm, 0, page, 3, 0x22, -1, 0) == -12);
    CHECK(!artbox_vm_mprotect(vm, static_cast<uint64_t>(first), page, 1));
    CHECK(artbox_vm_write(vm, static_cast<uint64_t>(first), &value, sizeof(value)) == -14);
    CHECK(artbox_vm_mmap(vm, static_cast<uint64_t>(first), page, 3, 0x32, -1, 0) == first);
    CHECK(!artbox_vm_read(vm, static_cast<uint64_t>(first), &readback, sizeof(readback)) && readback == 0);
    CHECK(!artbox_vm_munmap(vm, window.base, window.length));

    std::atomic<unsigned> failures{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i) threads.emplace_back([&, i] {
        for (unsigned n = 0; n < 32; ++n) {
            int64_t address = artbox_vm_mmap(vm, 0, page, 3, 0x22, -1, 0);
            if (address < 0) { ++failures; return; }
            uint64_t target = static_cast<uint64_t>(address), marker = (uint64_t(i + 1) << 32) | n, observed = 0;
            if (artbox_vm_write(vm, target, &marker, sizeof(marker)) ||
                artbox_vm_read(vm, target, &observed, sizeof(observed)) || observed != marker ||
                artbox_vm_munmap(vm, target, page)) ++failures;
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK(!failures && reservations == 1 && releases == 0);
    CHECK(!artbox_vm_destroy(vm) && releases == 1);

    vm = artbox_vm_create_window(&ops, page * 4, page, &window);
    CHECK(vm);
    fail_reset = true;
    CHECK(artbox_vm_mmap(vm, 0, page, 3, 0x22, -1, 0) == -12);
    CHECK(!artbox_vm_access(vm, window.base + page, page, 3));
    CHECK(artbox_vm_mmap(vm, 0, page, 3, 0x22, -1, 0) == -5);
    CHECK(!artbox_vm_destroy(vm));
    fail_reset = false;

    // Reserve the codec's full range while committing only its final page.
    vm = artbox_vm_create_window(&ops, UINT64_C(0x100000000), page, &window);
    CHECK(vm);
    uint64_t last = window.base + window.length - page;
    CHECK(artbox_vm_mmap(vm, last, page, 3, 0x32, -1, 0) == static_cast<int64_t>(last));
    CHECK(!artbox_reference_encode(&window, window.base + UINT64_C(0xfffffff8), &reference));
    CHECK(reference == UINT32_C(0xfffffff8));
    CHECK(!artbox_reference_decode(&window, reference, &decoded));
    CHECK(!artbox_vm_write(vm, decoded, &value, sizeof(value)));
    CHECK(!artbox_vm_read(vm, decoded, &readback, sizeof(readback)) && readback == value);
    CHECK(artbox_vm_mmap(vm, last, page + 1, 3, 0x32, -1, 0) == -95);
    CHECK(!artbox_vm_destroy(vm) && reservations == releases);
    std::puts("stable managed window: allocation, reuse, guard, concurrency, mutation failure and 4 GiB boundary passed");
}
