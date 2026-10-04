// Run the shared threaded fixture through guest syscalls. SPDX-License-Identifier: MIT
#include "artbox/binder_device.h"
#include "artbox/native_files.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include "../fixtures/binder-transaction/check.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
struct Context { artbox_vfs *fs; artbox_kernel_thread thread; uint64_t path; bool same_pid = true; };
static int32_t pid_for(void *opaque, int32_t role) {
    return role == 2 && !static_cast<Context *>(opaque)->same_pid ? 101 : 100;
}
static int open_device(void *opaque, int32_t role) {
    auto *c = static_cast<Context *>(opaque);
    auto thread = c->thread; thread.pid = pid_for(opaque, role);
    return static_cast<int>(artbox_vfs_call(c->fs, &thread, 56, UINT32_C(0xffffff9c), c->path, 0x80802, 0));
}
static int close_device(void *opaque, int fd) {
    auto *c = static_cast<Context *>(opaque);
    return static_cast<int>(artbox_vfs_call(c->fs, &c->thread, 57, static_cast<uint64_t>(fd), 0, 0, 0));
}
static int64_t map_device(void *opaque, int fd, size_t length) {
    auto *c = static_cast<Context *>(opaque);
    return artbox_vfs_mmap(c->fs, c->thread.vm, 0, length, 1, 2, fd, 0);
}
static int unmap_device(void *opaque, uint64_t address, size_t length) {
    return artbox_vm_munmap(static_cast<Context *>(opaque)->thread.vm, address, length);
}
static int64_t ioctl_device(void *opaque, int fd, int32_t tid, uint32_t request, uint64_t argument) {
    auto *c = static_cast<Context *>(opaque);
    auto thread = c->thread; thread.tid = tid; thread.pid = pid_for(opaque, tid);
    return artbox_vfs_call(c->fs, &thread, 29, static_cast<uint64_t>(fd), request, argument, 0);
}
static int read_bytes(void *opaque, uint64_t address, void *out, size_t length) {
    return artbox_vm_read(static_cast<Context *>(opaque)->thread.vm, address, out, length);
}
static void pause_device(void *) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
static int parallel(void *, int (*left)(void *), void *left_arg,
    int (*right)(void *), void *right_arg, int results[2]) {
    std::thread worker([&] { results[0] = left(left_arg); });
    results[1] = right(right_arg);
    worker.join();
    return 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    artbox_native_files *native = nullptr;
    int result = artbox_native_files_open(argv[1], &native);
    if (result == -95) { std::puts("Native transaction provider unavailable"); return 77; }
    CHECK(result == 0);
    auto memory = artbox_native_vm();
    auto files = artbox_native_files_ops(native);
    const size_t page = memory.page_size;
    artbox_vm *vm = artbox_vm_create(&memory, page * 64, 16);
    artbox_binder_device *device = artbox_binder_device_create(4, 4);
    CHECK(vm && device);
    const artbox_binder_memory_ops backing = {memory, files.mapping, native, artbox_native_files_temporary, files.close};
    CHECK(artbox_binder_device_set_memory(device, &backing, page * 16) == 0);
    int64_t args = artbox_vm_mmap(vm, 0, page * 3, 3, 0x22, -1, 0);
    CHECK(args > 0);
    Context context{}; context.fs = artbox_vfs_create(nullptr, 8); context.path = static_cast<uint64_t>(args);
    auto system = artbox_native_system();
    CHECK(context.fs && artbox_kernel_thread_init(&context.thread, vm, &system, 100, 100) == 0);
    std::memcpy(reinterpret_cast<void *>(context.path), "/dev/binder", 12);
    CHECK(artbox_vfs_set_binder(context.fs, device, 10000) == 0);
    const artbox_binder_transaction_ops ops = {pid_for, 10000, page, open_device, close_device, map_device,
        unmap_device, ioctl_device, read_bytes, pause_device, parallel};
    auto *server = reinterpret_cast<artbox_binder_transaction_scratch *>(context.path + page);
    auto *client = reinterpret_cast<artbox_binder_transaction_scratch *>(context.path + page * 2);
    CHECK(artbox_binder_transaction_same_pid_check(&context, &ops, server, client) == 0);
    context.same_pid = false;
    CHECK(artbox_binder_transaction_check(&context, &ops, server, client) == 0);
    CHECK(artbox_vfs_destroy(context.fs) == 0 && artbox_binder_device_destroy(device) == 0);
    CHECK(artbox_vm_destroy(vm) == 0 && artbox_native_files_close(native) == 0);
    std::puts("{\"same_pid_rejected\":true,\"shared_threaded_ping_pong\":true,\"native_aliases\":true,\"cleanup\":true,\"passed\":true}");
}
