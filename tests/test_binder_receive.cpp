// Original Binder receive mapping and owner-lifetime controls. SPDX-License-Identifier: MIT
#include "artbox/binder_device.h"
#include "artbox/native_files.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include "../fixtures/binder-mapping/check.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

struct Backing;
struct Provider {
    artbox_vm_ops memory{};
    artbox_native_files *native = nullptr;
    artbox_file_ops files{};
    std::atomic<unsigned> live{0};
    unsigned creates = 0, fail_map = 0;
    bool fail_create = false;
    std::atomic<bool> block{false}, entered{false}, proceed{false};
    Backing *last = nullptr;
};
struct Backing {
    Provider *owner;
    size_t length;
    std::atomic<unsigned> references{1};
    unsigned maps = 0;
    void *native = nullptr, *driver_alias = nullptr;
    Backing(Provider *p, size_t size) : owner(p), length(size) {}
};
static int create_backing(void *context, size_t length, void **out) {
    auto *p = static_cast<Provider *>(context);
    ++p->creates;
    if (p->fail_create) return -28;
    auto *file = new Backing(p, length);
    if (p->native) {
        int result = artbox_native_files_temporary(p->native, length, &file->native);
        if (result) { delete file; return result; }
    }
    ++p->live; p->last = file; *out = file;
    return 0;
}
static int close_backing(void *opaque) {
    auto *file = static_cast<Backing *>(opaque);
    if (--file->references) return 0;
    int result = file->native ? file->owner->files.close(file->native) : 0;
    --file->owner->live;
    delete file;
    return result;
}
static int acquire_backing(void *opaque, void **out) {
    ++static_cast<Backing *>(opaque)->references; *out = opaque; return 0;
}
static void release_backing(void *opaque) { CHECK(close_backing(opaque) == 0); }
static int map_backing(void *opaque, void *address, size_t length, unsigned prot,
                       unsigned sharing, uint64_t offset) {
    auto *file = static_cast<Backing *>(opaque);
    Provider &p = *file->owner;
    CHECK(length == file->length && sharing == 1 && !offset);
    ++file->maps;
    if (file->maps == p.fail_map) return -12;
    if (file->maps == 2 && p.block.load()) {
        p.entered.store(true);
        while (!p.proceed.load()) std::this_thread::yield();
    }
    int result = file->native ? p.files.mapping.map(file->native, address, length, prot, sharing, offset)
                              : p.memory.reset(address, length, prot);
    if (!result && prot == 3) file->driver_alias = address;
    return result;
}
static int sync_backing(void *, void *, size_t, unsigned) { return 0; }
struct Context { artbox_vfs *fs; artbox_kernel_thread thread; uint64_t path; };
static int open_flags(void *opaque, uint32_t flags) {
    auto *c = static_cast<Context *>(opaque);
    return static_cast<int>(artbox_vfs_call(c->fs, &c->thread, 56, UINT32_C(0xffffff9c), c->path, flags, 0));
}
static int open_device(void *opaque) { return open_flags(opaque, 0x80802); }
static int close_device(void *opaque, int fd) {
    auto *c = static_cast<Context *>(opaque);
    return static_cast<int>(artbox_vfs_call(c->fs, &c->thread, 57, static_cast<uint64_t>(fd), 0, 0, 0));
}
static int64_t ioctl_device(void *opaque, int fd, uint32_t request, uint64_t argument) {
    auto *c = static_cast<Context *>(opaque);
    return artbox_vfs_call(c->fs, &c->thread, 29, static_cast<uint64_t>(fd), request, argument, 0);
}
static void pause_device(void *) { std::this_thread::yield(); }
static int64_t map_device(void *opaque, int fd, uint64_t length, unsigned prot, unsigned flags, uint64_t offset) {
    auto *c = static_cast<Context *>(opaque);
    return artbox_vfs_mmap(c->fs, c->thread.vm, 0, length, prot, flags, fd, offset);
}
static int protect_device(void *opaque, uint64_t address, uint64_t length, unsigned prot) {
    return artbox_vm_mprotect(static_cast<Context *>(opaque)->thread.vm, address, length, prot);
}
static int unmap_device(void *opaque, uint64_t address, uint64_t length) {
    return artbox_vm_munmap(static_cast<Context *>(opaque)->thread.vm, address, length);
}
int main(int argc, char **argv) {
    CHECK(argc == 1 || argc == 2);
    Provider provider;
    provider.memory = artbox_native_vm();
    const size_t page = provider.memory.page_size;
    if (argc == 2) {
        int result = artbox_native_files_open(argv[1], &provider.native);
        if (result == -95) { std::puts("Native receive provider unavailable"); return 77; }
        CHECK(result == 0);
        provider.files = artbox_native_files_ops(provider.native);
        void *unchanged = reinterpret_cast<void *>(uintptr_t(1));
        CHECK(artbox_native_files_temporary(provider.native, 0, &unchanged) == -22);
        CHECK(unchanged == reinterpret_cast<void *>(uintptr_t(1)));
    }
    const artbox_binder_memory_ops backing = {provider.memory,
        {acquire_backing, release_backing, map_backing, sync_backing}, &provider, create_backing, close_backing};
    artbox_binder_device *device = artbox_binder_device_create(8, 4);
    CHECK(device);
    CHECK(artbox_binder_device_set_memory(device, nullptr, page * 4) == -22);
    CHECK(artbox_binder_device_set_memory(device, &backing, page + 1) == -22);
    CHECK(artbox_binder_device_set_memory(device, &backing, page * 4) == 0);
    CHECK(artbox_binder_device_set_memory(device, &backing, page * 4) == -114);
    artbox_vm *vm = artbox_vm_create(&provider.memory, page * 16, 16);
    CHECK(vm);
    int64_t scratch = artbox_vm_mmap(vm, 0, page, 3, 0x22, -1, 0);
    CHECK(scratch > 0);
    Context c{}; c.fs = artbox_vfs_create(nullptr, 8); c.path = static_cast<uint64_t>(scratch);
    auto system = artbox_native_system();
    CHECK(c.fs && artbox_kernel_thread_init(&c.thread, vm, &system, 100, 100) == 0);
    std::memcpy(reinterpret_cast<void *>(c.path), "/dev/binder", 12);
    CHECK(artbox_vfs_set_binder(c.fs, device, 10000) == 0);
    const artbox_binder_device_ops ioctls = {open_device, close_device, ioctl_device, pause_device};
    const artbox_binder_mapping_ops memory = {page, map_device, protect_device, unmap_device, open_flags};
    CHECK(artbox_binder_mapping_check(&c, &ioctls, &memory) == 35);

    int fd = open_device(&c); CHECK(fd >= 3); // Also reaps the fixture's final closed mapping.
    CHECK(provider.live == 0);
    // The native reference rejects handle-zero self-calls even across opens.
    // This control does not require alias coherence and runs on every host.
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    int same_pid = open_device(&c); CHECK(same_pid >= 3);
    auto *transfer = reinterpret_cast<uint64_t *>(c.path + 128);
    auto *command = reinterpret_cast<uint32_t *>(c.path + 256);
    auto *reply = reinterpret_cast<uint32_t *>(c.path + 512);
    std::memset(transfer, 0, 48); std::memset(command, 0, 68);
    command[0] = ARTBOX_BC_TRANSACTION;
    transfer[0] = 68; transfer[2] = c.path + 256;
    CHECK(ioctl_device(&c, same_pid, ARTBOX_BINDER_WRITE_READ, c.path + 128) == 0 && transfer[1] == 68);
    std::memset(transfer, 0, 48); transfer[3] = 128; transfer[5] = c.path + 512;
    CHECK(ioctl_device(&c, same_pid, ARTBOX_BINDER_WRITE_READ, c.path + 128) == 0);
    CHECK(transfer[4] == 8 && reply[0] == ARTBOX_BR_NOOP && reply[1] == ARTBOX_BR_FAILED_REPLY);
    transfer[4] = 0;
    CHECK(ioctl_device(&c, same_pid, ARTBOX_BINDER_WRITE_READ, c.path + 128) == -11 && transfer[4] == 0);
    transfer[5] = 1;
    CHECK(ioctl_device(&c, same_pid, ARTBOX_BINDER_WRITE_READ, c.path + 128) == -14);
    CHECK(close_device(&c, same_pid) == 0 && close_device(&c, fd) == 0);
    fd = open_device(&c); CHECK(fd >= 3);
    // A queued synchronous caller must survive descriptor close while the
    // manager mapping remains, then receive failure after final owner loss.
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    int64_t abandoned = map_device(&c, fd, page, 1, 2, 0); CHECK(abandoned > 0);
    Context caller = c; caller.thread.pid = 101; caller.thread.tid = 101;
    int waiting = open_device(&caller); CHECK(waiting >= 3);
    std::memset(transfer, 0, 48); std::memset(command, 0, 68);
    command[0] = ARTBOX_BC_TRANSACTION;
    transfer[0] = 68; transfer[2] = c.path + 256;
    CHECK(ioctl_device(&caller, waiting, ARTBOX_BINDER_WRITE_READ, c.path + 128) == 0);
    std::memset(transfer, 0, 48); transfer[3] = 128; transfer[5] = c.path + 512;
    CHECK(ioctl_device(&caller, waiting, ARTBOX_BINDER_WRITE_READ, c.path + 128) == 0);
    CHECK(transfer[4] == 8 && reply[1] == ARTBOX_BR_TRANSACTION_COMPLETE);
    CHECK(close_device(&c, fd) == 0);
    transfer[4] = 0;
    CHECK(ioctl_device(&caller, waiting, ARTBOX_BINDER_WRITE_READ, c.path + 128) == -11);
    CHECK(unmap_device(&c, static_cast<uint64_t>(abandoned), page) == 0);
    CHECK(ioctl_device(&caller, waiting, ARTBOX_BINDER_WRITE_READ, c.path + 128) == 0);
    CHECK(transfer[4] == 8 && reply[1] == ARTBOX_BR_DEAD_REPLY && provider.live == 0);
    CHECK(close_device(&caller, waiting) == 0 && artbox_vm_reserved_bytes(vm) == page);
    fd = open_device(&c); CHECK(fd >= 3);
    const unsigned creates = provider.creates;
    artbox_vm *foreign_vm = artbox_vm_create(&provider.memory, page, 1);
    CHECK(foreign_vm);
    CHECK(artbox_vfs_mmap(c.fs, foreign_vm, 0, page, 1, 2, fd, 0) == -95);
    CHECK(artbox_vm_destroy(foreign_vm) == 0);
    CHECK(map_device(&c, fd, page, 4, 2, 0) == -1);
    CHECK(map_device(&c, fd, page * 5, 1, 2, 0) == -12);
    CHECK(map_device(&c, fd, page, 1, 2, page) == -95); // Not yet compared.
    CHECK(map_device(&c, fd, page, 1, 0x12, 0) == -95); // Fixed receive mappings not enabled.
    CHECK(provider.creates == creates);
    provider.fail_create = true;
    CHECK(map_device(&c, fd, page, 1, 2, 0) == -28);
    provider.fail_create = false;
    for (unsigned failure = 1; failure <= 2; ++failure) {
        provider.fail_map = failure;
        CHECK(map_device(&c, fd, page, 1, 2, 0) == -12);
        CHECK(provider.live == 0 && artbox_vm_reserved_bytes(vm) == page);
    }
    provider.fail_map = 0;
    int64_t mapped = map_device(&c, fd, page * 2, 1, 2, 0);
    CHECK(mapped > 0 && provider.live == 1);
    uint64_t address = static_cast<uint64_t>(mapped);
    CHECK(provider.last->driver_alias && !artbox_vm_access(vm,
        reinterpret_cast<uintptr_t>(provider.last->driver_alias), page, 1));
    if (provider.native) {
        const char text[] = "driver writes are visible in the guest's private Binder map";
        std::memcpy(provider.last->driver_alias, text, sizeof(text));
        char copied[sizeof(text)];
        CHECK(artbox_vm_read(vm, address, copied, sizeof(copied)) == 0);
        CHECK(!std::memcmp(copied, text, sizeof(text)));
    }
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    CHECK(close_device(&c, fd) == 0);
    CHECK(artbox_binder_device_destroy(device) == -16);
    fd = open_device(&c); CHECK(fd >= 3);
    CHECK(artbox_vm_mmap(vm, address, page, 3, 0x32, -1, 0) == mapped);
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16);
    CHECK(artbox_vm_mmap(vm, address + page, page, 3, 0x32, -1, 0) == mapped + static_cast<int64_t>(page));
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    CHECK(provider.live == 0); // Anonymous reservation pages do not retain Binder.
    CHECK(unmap_device(&c, address, page * 2) == 0);

    // mmap pins the original open description without holding the VFS lock.
    // Descriptor close must complete while the guest-map callback is blocked.
    provider.block.store(true);
    std::thread mapper([&] { mapped = map_device(&c, fd, page, 1, 2, 0); });
    while (!provider.entered.load()) std::this_thread::yield();
    CHECK(close_device(&c, fd) == 0);
    provider.proceed.store(true);
    mapper.join(); provider.block.store(false);
    CHECK(mapped > 0 && provider.live == 1);
    fd = open_device(&c); CHECK(fd >= 3);
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16);
    CHECK(unmap_device(&c, static_cast<uint64_t>(mapped), page) == 0);
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    CHECK(provider.live == 0);
    CHECK(map_device(&c, fd, page, 1, 1, 0) > 0);
    CHECK(artbox_vfs_destroy(c.fs) == 0);
    CHECK(artbox_binder_device_destroy(device) == -16);
    CHECK(artbox_vm_destroy(vm) == 0);
    CHECK(artbox_binder_device_destroy(device) == 0 && provider.live == 0);
    if (provider.native) CHECK(artbox_native_files_close(provider.native) == 0);
    std::printf("{\"shared_mapping_cases\":35,\"native_alias_verified\":%s,\"ownership_controls\":true,\"passed\":true}\n",
        argc == 2 ? "true" : "false");
}
