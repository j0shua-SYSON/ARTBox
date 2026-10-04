// Shared Linux fixture plus original resource/VM contracts. SPDX-License-Identifier: MIT
#include "artbox/binder_device.h"
#include "artbox/native_vm.h"
#include "../fixtures/binder-device/check.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
struct Context { artbox_binder_device *device; artbox_vm *vm; };
static int open_device(void *p) {
    auto *c = static_cast<Context *>(p);
    uint64_t token = 0;
    int result = artbox_binder_device_open(c->device, c->vm, 100, 1000, &token);
    CHECK(result || token <= INT32_MAX);
    return result ? result : static_cast<int>(token);
}
static int close_device(void *p, int fd) {
    return artbox_binder_device_close(static_cast<Context *>(p)->device, static_cast<uint64_t>(fd));
}
static int64_t ioctl_device(void *p, int fd, uint32_t request, uint64_t argument) {
    return artbox_binder_device_ioctl(static_cast<Context *>(p)->device, static_cast<uint64_t>(fd), 100, request, argument);
}
static void pause_device(void *) { std::this_thread::yield(); }

int main() {
    artbox_vm_ops memory = artbox_native_vm();
    Context c = {artbox_binder_device_create(4, 2), artbox_vm_create(&memory, memory.page_size * 2, 4)};
    CHECK(c.device && c.vm);
    int64_t storage = artbox_vm_mmap(c.vm, 0, memory.page_size * 2, 3, 0x22, -1, 0);
    CHECK(storage > 0);
    uint64_t address = static_cast<uint64_t>(storage);
    auto *scratch = reinterpret_cast<artbox_binder_device_scratch *>(address);
    const artbox_binder_device_ops ops = {open_device, close_device, ioctl_device, pause_device};
    CHECK(artbox_binder_device_check(&c, &ops, scratch) == 32);
    CHECK(artbox_binder_device_destroy(c.device) == 0);

    // Endpoint limits, monotonic tokens, fixed-UID context ownership and close.
    CHECK(!artbox_binder_device_create(0, 1));
    CHECK(!artbox_binder_device_create(1, 0));
    CHECK(!artbox_binder_device_create(1025, 1));
    CHECK(!artbox_binder_device_create(1, 1025));
    c.device = artbox_binder_device_create(2, 2);
    uint64_t a = 0, b = 0, output = UINT64_MAX;
    CHECK(artbox_binder_device_open(c.device, c.vm, 100, 1000, &a) == 0);
    CHECK(artbox_binder_device_open(c.device, c.vm, 100, 2000, &b) == 0 && b != a);
    CHECK(artbox_binder_device_open(c.device, c.vm, 100, 1000, &output) == -24 && output == UINT64_MAX);
    CHECK(artbox_binder_device_destroy(c.device) == -16);
    auto call = [&](uint64_t token, int tid, uint32_t request, uint64_t argument) {
        return artbox_binder_device_ioctl(c.device, token, tid, request, argument);
    };
    CHECK(call(a, 100, ARTBOX_BINDER_SET_CONTEXT_MGR, 1) == 0);
    CHECK(call(b, 100, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16); // Busy precedes UID.
    CHECK(artbox_binder_device_close(c.device, a) == 0);
    CHECK(call(b, 100, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -1); // UID persists after close.
    CHECK(call(a, 100, ARTBOX_BINDER_VERSION, address) == -9);
    CHECK(artbox_binder_device_close(c.device, a) == -9);
    CHECK(artbox_binder_device_open(c.device, c.vm, 100, 1000, &output) == 0 && output > b);
    a = output;
    CHECK(call(a, 100, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);

    // BINDER_SET_MAX_THREADS does not shrink the separate host admission cap.
    *reinterpret_cast<uint32_t *>(address) = 0;
    CHECK(call(a, 100, ARTBOX_BINDER_SET_MAX_THREADS, address) == 0);
    CHECK(call(a, 101, ARTBOX_BINDER_VERSION, address) == 0);
    CHECK(call(a, 102, ARTBOX_BINDER_VERSION, address) == -12);
    CHECK(call(a, 101, ARTBOX_BINDER_THREAD_EXIT, 0) == 0);
    CHECK(call(a, 102, ARTBOX_BINDER_VERSION, address) == 0);
    CHECK(call(a, 102, ARTBOX_BINDER_THREAD_EXIT, 0) == 0);

    // Copies must honor VM permissions and reject wrapped or partial ranges.
    CHECK(call(a, 100, ARTBOX_BINDER_VERSION, UINT64_MAX - 1) == -22);
    CHECK(call(a, 100, ARTBOX_BINDER_WRITE_READ, address + memory.page_size * 2 - 47) == -14);
    std::memset(scratch, 0, sizeof(*scratch));
    CHECK(artbox_vm_mprotect(c.vm, address, memory.page_size, 1) == 0);
    CHECK(call(a, 100, ARTBOX_BINDER_VERSION, address) == -22);
    CHECK(call(a, 100, ARTBOX_BINDER_WRITE_READ, address) == -14);
    CHECK(artbox_vm_mprotect(c.vm, address, memory.page_size, 3) == 0);
    scratch->write_read[0] = 8; scratch->write_read[2] = address + memory.page_size * 2 - 4;
    *reinterpret_cast<uint32_t *>(scratch->write_read[2]) = ARTBOX_BC_ENTER_LOOPER;
    scratch->write_read[4] = 71;
    CHECK(call(a, 100, ARTBOX_BINDER_WRITE_READ, address) == -14);
    CHECK(scratch->write_read[1] == 4 && scratch->write_read[4] == 0);

    // Known but unimplemented commands do not masquerade as successful work.
    std::memset(scratch, 0, sizeof(*scratch));
    scratch->commands[0] = ARTBOX_BC_TRANSACTION;
    scratch->write_read[0] = 4;
    scratch->write_read[2] = reinterpret_cast<uintptr_t>(scratch->commands);
    CHECK(call(a, 100, ARTBOX_BINDER_WRITE_READ, address) == -95);
    CHECK(scratch->write_read[1] == 0);
    scratch->write_read[0] = 65537;
    CHECK(call(a, 100, ARTBOX_BINDER_WRITE_READ, address) == -7);
    CHECK(scratch->write_read[1] == 0);
    scratch->write_read[0] = 0; scratch->write_read[3] = 4;
    CHECK(call(a, 100, ARTBOX_BINDER_WRITE_READ, address) == -95);
    CHECK(call(a, 0, ARTBOX_BINDER_VERSION, address) == -22);
    CHECK(artbox_binder_device_close(c.device, a) == 0);
    CHECK(artbox_binder_device_close(c.device, b) == 0);
    CHECK(artbox_binder_device_destroy(c.device) == 0);

    // Distinct same-PID opens remain isolated while other threads close/reopen.
    c.device = artbox_binder_device_create(8, 1);
    CHECK(c.device);
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&, i] {
        for (int n = 0; n < 500; ++n) {
            uint64_t token = 0;
            CHECK(artbox_binder_device_open(c.device, c.vm, 100, 1000, &token) == 0);
            CHECK(call(token, 100 + i, ARTBOX_BINDER_THREAD_EXIT, 0) == 0);
            CHECK(artbox_binder_device_close(c.device, token) == 0);
            CHECK(call(token, 100 + i, ARTBOX_BINDER_THREAD_EXIT, 0) == -9);
        }
    });
    for (auto &worker : workers) worker.join();
    CHECK(artbox_binder_device_destroy(c.device) == 0);
    CHECK(artbox_vm_destroy(c.vm) == 0);
    std::puts("{\"protocol\":8,\"shared_cases\":32,\"vm_and_admission_controls\":true,\"concurrent_lifecycles\":4000,\"passed\":true}");
    return 0;
}
