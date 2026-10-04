#include "artbox/native_vm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <atomic>
#include <thread>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
// An injected backing tests ownership and mapper decisions on every host.
// Native shared-file coherence is tested separately through the rooted VFS.
struct File { std::vector<unsigned char> data; unsigned refs = 0, maps = 0, syncs = 0; bool fail = false; };
static artbox_vm_ops memory;
static int acquire(void *p, void **out) { ++static_cast<File*>(p)->refs; *out = p; return 0; }
static void release(void *p) { --static_cast<File*>(p)->refs; }
static int map(void *p, void *address, size_t length, unsigned protection, unsigned sharing, uint64_t offset) {
    File *f = static_cast<File*>(p); ++f->maps;
    CHECK(sharing == 1 || sharing == 2);
    if (f->fail) return -12;
    CHECK(offset <= f->data.size() && length <= f->data.size() - offset);
    int error = memory.reset(address, length, 3);
    if (error) return error;
    std::memcpy(address, f->data.data() + offset, length);
    return memory.protect(address, length, protection);
}
static int sync(void *p, void *, size_t, unsigned) { ++static_cast<File*>(p)->syncs; return 0; }
static int fail_reset(void *, size_t, unsigned) { return -5; }
static void lifetime_tracking() {
    const size_t page = memory.page_size;
    File file; file.data.resize(page * 4, 0x47);
    const artbox_vm_file_ops ops{acquire, release, map, sync};
    artbox_vm *vm = artbox_vm_create(&memory, page * 8, 8); CHECK(vm);
    auto *unchanged = reinterpret_cast<artbox_vm_mapping_watch *>(uintptr_t(1));
    artbox_vm_mapping_watch *watch = unchanged;
    CHECK(artbox_vm_map_file_watched(vm, 0, page, 1, 2, 0, &file, &ops, 1, nullptr) == -22);
    file.fail = true;
    CHECK(artbox_vm_map_file_watched(vm, 0, page, 1, 2, 0, &file, &ops, 1, &watch) == -12);
    CHECK(watch == unchanged && file.refs == 0 && artbox_vm_reserved_bytes(vm) == 0);
    file.fail = false;
    int64_t result = artbox_vm_map_file_watched(vm, 0, page * 2, 1, 2, 0, &file, &ops, 1, &watch);
    CHECK(result > 0 && watch && watch != unchanged && file.refs == 1);
    uint64_t address = static_cast<uint64_t>(result);
    CHECK(artbox_vm_mapping_watch_state(watch) == 3);
    CHECK(artbox_vm_mprotect(vm, address, page, 3) == -13);
    CHECK(artbox_vm_mprotect(vm, address, page, 0) == 0);
    CHECK(artbox_vm_mprotect(vm, address, page, 1) == 0);
    CHECK(artbox_vm_madvise(vm, address, page, 1) == 0);
    CHECK(artbox_vm_mapping_watch_state(watch) == 3);
    CHECK(artbox_vm_munmap(vm, address + 1, page) == -22);
    CHECK(artbox_vm_mapping_watch_state(watch) == 3);
    CHECK(artbox_vm_munmap(vm, address, page) == 0);
    CHECK(artbox_vm_mapping_watch_state(watch) == ARTBOX_VM_MAPPING_LIVE && file.refs == 1);
    CHECK(artbox_vm_mmap(vm, address, page, 3, 0x32, -1, 0) == static_cast<int64_t>(address));
    CHECK(artbox_vm_mapping_watch_state(watch) == ARTBOX_VM_MAPPING_LIVE);
    // Final file-page replacement ends file ownership even when anonymous
    // pages still occupy the original native reservation.
    CHECK(artbox_vm_mmap(vm, address + page, page, 3, 0x32, -1, 0) == static_cast<int64_t>(address + page));
    CHECK(artbox_vm_mapping_watch_state(watch) == 0 && file.refs == 0);
    CHECK(artbox_vm_munmap(vm, address, page * 2) == 0);
    artbox_vm_mapping_watch_destroy(watch);

    result = artbox_vm_map_file_watched(vm, 0, page * 2, 1, 2, 0, &file, &ops, 1, &watch);
    CHECK(result > 0); address = static_cast<uint64_t>(result);
    CHECK(artbox_vm_mmap(vm, address, page, 3, 0x32, -1, 0) == result);
    CHECK(artbox_vm_mapping_watch_state(watch) == ARTBOX_VM_MAPPING_LIVE);
    CHECK(artbox_vm_munmap(vm, address + page, page) == 0);
    CHECK(artbox_vm_mapping_watch_state(watch) == 0 && file.refs == 0);
    CHECK(artbox_vm_destroy(vm) == 0);
    CHECK(artbox_vm_mapping_watch_state(watch) == 0); // No borrowed VM pointer.
    artbox_vm_mapping_watch_destroy(watch);

    // Dropping observation must not close the file or invalidate the view.
    vm = artbox_vm_create(&memory, page * 8, 8); CHECK(vm);
    result = artbox_vm_map_file_watched(vm, 0, page, 1, 2, 0, &file, &ops, 1, &watch);
    CHECK(result > 0 && file.refs == 1);
    artbox_vm_mapping_watch_destroy(watch);
    CHECK(file.refs == 1 && *reinterpret_cast<unsigned char *>(result) == 0x47);
    CHECK(artbox_vm_destroy(vm) == 0 && file.refs == 0);

    // A failed native replacement poisons VM ownership: do not keep claiming
    // the original file range is intact, even if this injected provider did
    // not actually change any pages before reporting its error.
    artbox_vm_ops broken = memory; broken.reset = fail_reset;
    vm = artbox_vm_create(&broken, page * 8, 8); CHECK(vm);
    result = artbox_vm_map_file_watched(vm, 0, page, 1, 2, 0, &file, &ops, 1, &watch);
    CHECK(result > 0);
    CHECK(artbox_vm_mmap(vm, static_cast<uint64_t>(result), page, 3, 0x32, -1, 0) == -5);
    CHECK(artbox_vm_mapping_watch_state(watch) == ARTBOX_VM_MAPPING_LIVE && file.refs == 1);
    CHECK(!artbox_vm_access(vm, static_cast<uint64_t>(result), 1, 1));
    CHECK(artbox_vm_destroy(vm) == 0 && file.refs == 0);
    CHECK(artbox_vm_mapping_watch_state(watch) == 0);
    artbox_vm_mapping_watch_destroy(watch);

    // Watches can be read during native unmap/VM teardown without retaining
    // file resources or calling back into the device owning them.
    for (int iteration = 0; iteration < 128; ++iteration) {
        vm = artbox_vm_create(&memory, page * 8, 8); CHECK(vm);
        result = artbox_vm_map_file_watched(vm, 0, page * 2, 1, 2, 0, &file, &ops, 1, &watch);
        CHECK(result > 0); address = static_cast<uint64_t>(result);
        std::atomic<bool> ready{false}, done{false};
        std::thread reader([&] {
            unsigned previous = artbox_vm_mapping_watch_state(watch);
            CHECK(previous == 3);
            ready.store(true, std::memory_order_release);
            while (!done.load(std::memory_order_acquire)) {
                unsigned current = artbox_vm_mapping_watch_state(watch);
                CHECK(!(current & ~previous));
                previous = current;
            }
        });
        while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
        CHECK(artbox_vm_munmap(vm, address, page) == 0);
        CHECK(artbox_vm_destroy(vm) == 0 && file.refs == 0);
        CHECK(artbox_vm_mapping_watch_state(watch) == 0);
        done.store(true, std::memory_order_release);
        reader.join();
        artbox_vm_mapping_watch_destroy(watch);
    }
    CHECK(artbox_vm_mapping_watch_state(nullptr) == 0);
    artbox_vm_mapping_watch_destroy(nullptr);
}
int main() {
    memory = artbox_native_vm(); size_t page = memory.page_size;
    artbox_vm *vm = artbox_vm_create(&memory, page * 16, 8); CHECK(vm);
    File file; file.data.resize(page * 4, 0x31);
    std::memset(file.data.data() + page * 2, 0x72, page);
    const artbox_vm_file_ops ops{acquire, release, map, sync};
    CHECK(artbox_vm_map_file(vm, 0, page, 5, 2, 0, &file, &ops, 1) == -1);
    CHECK(artbox_vm_map_file(vm, 0, page, 3, 1, 0, &file, &ops, 1) == -13);
    CHECK(artbox_vm_map_file(vm, 0, page, 1, 2, 1, &file, &ops, 3) == -22);
    CHECK(artbox_vm_map_file(vm, 0, page, 1, 0, 0, &file, &ops, 3) == -22);
    CHECK(artbox_vm_map_file(vm, 0, page, 1, 0x12, 0, &file, &ops, 3) == -95);
    CHECK(file.refs == 0 && file.maps == 0);
    int64_t result = artbox_vm_map_file(vm, 0, page * 3, 3, 2, page, &file, &ops, 3);
    CHECK(result > 0 && file.refs == 1);
    uint64_t address = static_cast<uint64_t>(result);
    auto *bytes = reinterpret_cast<unsigned char*>(address);
    CHECK(bytes[0] == 0x31 && bytes[page] == 0x72);
    bytes[0] = 0x99;
    CHECK(file.data[page] == 0x31);
    CHECK(artbox_vm_mprotect(vm, address, page, 1) == 0);
    unsigned maps_before_hint = file.maps;
    CHECK(artbox_vm_madvise(vm, address, page - 1, 1) == 0 && bytes[0] == 0x99);
    CHECK(file.maps == maps_before_hint && file.syncs == 0 && file.refs == 1);
    CHECK(!artbox_vm_access(vm, address, 1, 2));
    CHECK(artbox_vm_madvise(vm, address, page, 4) == 0 && bytes[0] == 0x31);
    CHECK(!artbox_vm_access(vm, address, 1, 2));
    CHECK(artbox_vm_munmap(vm, address + page, page) == 0 && file.refs == 1);
    CHECK(artbox_vm_mmap(vm, address + page, page, 3, 0x32, -1, 0) == static_cast<int64_t>(address + page));
    bytes[page] = 0x88;
    CHECK(artbox_vm_madvise(vm, address, page * 3, 4) == 0);
    CHECK(bytes[0] == 0x31 && bytes[page] == 0 && bytes[page * 2] == 0x31);
    CHECK(artbox_vm_msync(vm, address + 1, page, 4) == -22);
    CHECK(artbox_vm_msync(vm, address, page, 5) == -22);
    CHECK(artbox_vm_msync(vm, address, page * 3, 4) == 0 && file.syncs == 0); // PRIVATE never writes back.
    CHECK(artbox_vm_munmap(vm, address, page * 3) == 0 && file.refs == 0);
    result = artbox_vm_map_file(vm, 0, page, 1, 1, 0, &file, &ops, 1);
    CHECK(result > 0); address = static_cast<uint64_t>(result);
    maps_before_hint = file.maps;
    CHECK(artbox_vm_madvise(vm, address, page - 1, 1) == 0);
    CHECK(*reinterpret_cast<unsigned char*>(address) == 0x31);
    CHECK(file.maps == maps_before_hint && file.syncs == 0 && file.refs == 1);
    CHECK(!artbox_vm_access(vm, address, 1, 2));
    CHECK(artbox_vm_mprotect(vm, address, page, 0) == 0);
    CHECK(artbox_vm_mprotect(vm, address, page, 3) == -13);
    CHECK(artbox_vm_mprotect(vm, address, page, 1) == 0);
    CHECK(artbox_vm_msync(vm, address, page, 4) == 0 && file.syncs == 1);
    CHECK(artbox_vm_munmap(vm, address, page) == 0 && file.refs == 0);
    file.fail = true;
    CHECK(artbox_vm_map_file(vm, 0, page, 1, 2, 0, &file, &ops, 3) == -12);
    CHECK(file.refs == 0 && artbox_vm_reserved_bytes(vm) == 0);
    file.fail = false;
    CHECK(artbox_vm_map_file(vm, 0, page, 1, 2, 0, &file, &ops, 3) > 0);
    CHECK(artbox_vm_destroy(vm) == 0 && file.refs == 0);
    lifetime_tracking();
    std::puts("File mapping ownership, discard, permission ceilings and rollback passed");
}
