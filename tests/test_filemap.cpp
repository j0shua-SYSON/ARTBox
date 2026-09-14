#include "artbox/native_vm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
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
    std::puts("File mapping ownership, discard, permission ceilings and rollback passed");
}
