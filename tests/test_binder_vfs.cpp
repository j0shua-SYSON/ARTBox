// Original Binder syscall routing/lifetime contract. SPDX-License-Identifier: MIT
#include "artbox/vfs.h"
#include "artbox/binder_device.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include "../fixtures/binder-device/check.h"
#include "../fixtures/binder-file/check.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
struct Context {
    artbox_vfs *fs;
    artbox_kernel_thread thread;
    uint64_t path, stat;
};
static int open_file(void *p, uint32_t flags) {
    auto *c = static_cast<Context *>(p);
    return static_cast<int>(artbox_vfs_call(c->fs, &c->thread, 56, UINT32_C(0xffffff9c), c->path, flags, 0600));
}
static int open_device(void *p) { return open_file(p, 0x80802); }
static int close_file(void *p, int fd) {
    auto *c = static_cast<Context *>(p);
    return static_cast<int>(artbox_vfs_call(c->fs, &c->thread, 57, static_cast<uint64_t>(fd), 0, 0, 0));
}
static int64_t ioctl_device(void *p, int fd, uint32_t request, uint64_t argument) {
    auto *c = static_cast<Context *>(p);
    return artbox_vfs_call(c->fs, &c->thread, 29, static_cast<uint64_t>(fd), request, argument, 0);
}
static int64_t read_file(void *p, int fd, uint64_t address, uint64_t size) {
    auto *c = static_cast<Context *>(p);
    return artbox_vfs_call(c->fs, &c->thread, 63, static_cast<uint64_t>(fd), address, size, 0);
}
static int64_t write_file(void *p, int fd, uint64_t address, uint64_t size) {
    auto *c = static_cast<Context *>(p);
    return artbox_vfs_call(c->fs, &c->thread, 64, static_cast<uint64_t>(fd), address, size, 0);
}
static int64_t seek_file(void *p, int fd, int64_t offset, unsigned origin) {
    auto *c = static_cast<Context *>(p);
    return artbox_vfs_call(c->fs, &c->thread, 62, static_cast<uint64_t>(fd), static_cast<uint64_t>(offset), origin, 0);
}
static int type_file(void *p, int fd) {
    auto *c = static_cast<Context *>(p);
    int64_t result = artbox_vfs_call(c->fs, &c->thread, 80, static_cast<uint64_t>(fd), c->stat, 0, 0);
    if (result) return static_cast<int>(result);
    uint32_t mode = 0;
    CHECK(artbox_vm_read(c->thread.vm, c->stat + 16, &mode, 4) == 0);
    return static_cast<int>(mode & 0170000);
}
static void pause_device(void *) { std::this_thread::yield(); }
int main() {
    artbox_vm_ops memory = artbox_native_vm();
    artbox_system_ops system = artbox_native_system();
    artbox_vm *vm = artbox_vm_create(&memory, memory.page_size, 4);
    CHECK(vm);
    int64_t map = artbox_vm_mmap(vm, 0, memory.page_size, 3, 0x22, -1, 0);
    CHECK(map > 0);
    uint64_t address = static_cast<uint64_t>(map);
    auto *scratch = reinterpret_cast<artbox_binder_device_scratch *>(address);
    Context c{};
    c.fs = artbox_vfs_create(nullptr, 8); c.path = address + 512; c.stat = address + 256;
    CHECK(c.fs && artbox_kernel_thread_init(&c.thread, vm, &system, 100, 100) == 0);
    std::memcpy(reinterpret_cast<void *>(c.path), "/dev/binder", 12);
    CHECK(open_device(&c) == -2); // An unconfigured VFS exposes no device.
    artbox_binder_device *device = artbox_binder_device_create(8, 4);
    CHECK(device && artbox_vfs_set_binder(c.fs, device, 10000) == 0);
    CHECK(artbox_vfs_set_binder(c.fs, device, 10000) == -114);
    const artbox_binder_device_ops ioctls = {open_device, close_file, ioctl_device, pause_device};
    CHECK(artbox_binder_device_check(&c, &ioctls, scratch) == 32);
    const artbox_binder_file_ops files = {open_file, close_file, read_file, write_file, seek_file, type_file};
    CHECK(artbox_binder_file_check(&c, &files, scratch) == 22);
    int fd = open_device(&c);
    CHECK(fd == 3);
    CHECK(artbox_binder_device_destroy(device) == -16);
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_VERSION, address) == 0);
    // Linux int descriptors/command words ignore high register bits.
    CHECK(artbox_vfs_call(c.fs, &c.thread, 29, (UINT64_C(1) << 32) | 3,
        (UINT64_C(1) << 32) | ARTBOX_BINDER_VERSION, address, 0) == 0);
    CHECK(ioctl_device(&c, 12345, ARTBOX_BINDER_VERSION, address) == -9);
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_VERSION, 1) == -22);
    artbox_vm *other_vm = artbox_vm_create(&memory, memory.page_size, 2);
    CHECK(other_vm);
    Context foreign = c; foreign.thread.vm = other_vm;
    CHECK(ioctl_device(&foreign, fd, ARTBOX_BINDER_VERSION, address) == -95);
    CHECK(artbox_vm_destroy(other_vm) == 0);
    CHECK(artbox_vfs_call(c.fs, &c.thread, 80, static_cast<uint64_t>(fd), 1, 0, 0) == -14);
    CHECK(artbox_vfs_mmap(c.fs, vm, 0, memory.page_size, 1, 2, fd, 0) == -19); // Not enabled yet.
    CHECK(close_file(&c, fd) == 0);

    // Directory-relative discovery and independent same-PID opens.
    std::memcpy(reinterpret_cast<void *>(c.path), "/dev", 5);
    int dir = open_file(&c, 0x84000);
    CHECK(dir == 3);
    std::memcpy(reinterpret_cast<void *>(c.path), "binder", 7);
    CHECK(artbox_vfs_call(c.fs, &c.thread, 56, static_cast<uint64_t>(dir), c.path, 2, 0) == 4);
    CHECK(ioctl_device(&c, 4, ARTBOX_BINDER_VERSION, address) == 0);
    CHECK(ioctl_device(&c, dir, UINT32_MAX, 0) == -25);
    CHECK(close_file(&c, 4) == 0 && close_file(&c, dir) == 0);
    std::memcpy(reinterpret_cast<void *>(c.path), "/dev/binder", 12);
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&, i] {
        Context local = c;
        local.thread.tid = 101 + i;
        for (int n = 0; n < 500; ++n) {
            int opened = open_device(&local);
            CHECK(opened >= 3);
            CHECK(ioctl_device(&local, opened, ARTBOX_BINDER_THREAD_EXIT, 0) == 0);
            CHECK(close_file(&local, opened) == 0);
        }
    });
    for (auto &worker : workers) worker.join();
    // Descriptor reuse must acquire a new endpoint; table destruction closes it.
    CHECK(open_device(&c) == 3);
    CHECK(artbox_vfs_destroy(c.fs) == 0);
    CHECK(artbox_binder_device_destroy(device) == 0);
    // A failed open does not publish a descriptor or strand an endpoint.
    c.fs = artbox_vfs_create(nullptr, 2);
    device = artbox_binder_device_create(1, 1);
    CHECK(c.fs && device && artbox_vfs_set_binder(c.fs, device, 10000) == 0);
    CHECK(open_device(&c) == 3);
    CHECK(open_device(&c) == -24);
    CHECK(close_file(&c, 3) == 0);
    CHECK(open_device(&c) == 3);
    CHECK(artbox_vfs_destroy(c.fs) == 0);
    CHECK(artbox_binder_device_destroy(device) == 0);
    CHECK(artbox_vm_destroy(vm) == 0);
    std::puts("{\"shared_ioctl_cases\":32,\"shared_file_cases\":22,\"concurrent_lifecycles\":4000,\"passed\":true}");
    return 0;
}
