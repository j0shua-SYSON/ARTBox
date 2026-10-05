// Original guest VFS test adapters and native wake observation. SPDX-License-Identifier: MIT
#ifndef ARTBOX_TEST_VFS_WAIT_HELPERS_H
#define ARTBOX_TEST_VFS_WAIT_HELPERS_H
#include "artbox/vfs.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include "artbox/native_wake.h"
#include "artbox/signals.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
struct Probe {
    artbox_wake_ops native = artbox_native_wake();
    std::atomic<unsigned> live{0}, waiting{0};
    std::atomic<bool> fail_create{false}, fail_wait{false};
};
struct Owner { Probe *probe; void *native; };
static inline int create_wake(void *context, void **output) {
    auto *probe = static_cast<Probe *>(context); *output = nullptr;
    if (probe->fail_create.exchange(false)) return -12;
    auto *owner = new (std::nothrow) Owner{probe, nullptr};
    if (!owner) return -12;
    int error = probe->native.create(probe->native.context, &owner->native);
    if (error) { delete owner; return error; }
    ++probe->live; *output = owner; return 0;
}
static inline int signal_wake(void *opaque) {
    auto *owner = static_cast<Owner *>(opaque); return owner->probe->native.signal(owner->native);
}
static inline int wait_wake(void *opaque, int timeout) {
    auto *owner = static_cast<Owner *>(opaque);
    if (owner->probe->fail_wait.exchange(false)) return -5;
    ++owner->probe->waiting;
    int result = owner->probe->native.wait(owner->native, timeout);
    --owner->probe->waiting; return result;
}
static inline int close_wake(void *opaque) {
    auto *owner = static_cast<Owner *>(opaque);
    int result = owner->probe->native.close(owner->native);
    --owner->probe->live; delete owner; return result;
}
static inline void await(std::atomic<unsigned> &value, unsigned target) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (value.load() < target && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    CHECK(value.load() >= target);
}
struct Context { artbox_vfs *fs; artbox_kernel_thread thread; uint64_t scratch; };
static inline int64_t call(Context &c, uint64_t n, uint64_t a = 0, uint64_t b = 0, uint64_t d = 0,
                    uint64_t e = 0, uint64_t f = 0, uint64_t g = 0) {
    return artbox_vfs_syscall(c.fs, &c.thread, n, a, b, d, e, f, g);
}
static inline int close_fd(void *opaque, int fd) {
    return static_cast<int>(call(*static_cast<Context *>(opaque), 57, static_cast<uint64_t>(fd)));
}
static inline int64_t read_fd(void *opaque, int fd, void *output, size_t size) {
    auto &c = *static_cast<Context *>(opaque);
    if (output) CHECK(artbox_vm_write(c.thread.vm, c.scratch, output, 16) == 0);
    int64_t result = call(c, 63, static_cast<uint64_t>(fd), output ? c.scratch : 0, size);
    if (output) CHECK(artbox_vm_read(c.thread.vm, c.scratch, output, 16) == 0);
    return result;
}
static inline int64_t write_fd(void *opaque, int fd, const void *input, size_t size) {
    auto &c = *static_cast<Context *>(opaque);
    if (input) CHECK(artbox_vm_write(c.thread.vm, c.scratch, input, 16) == 0);
    return call(c, 64, static_cast<uint64_t>(fd), input ? c.scratch : 0, size);
}
static inline int64_t seek_fd(void *opaque, int fd, int64_t offset, unsigned origin) {
    return call(*static_cast<Context *>(opaque), 62, static_cast<uint64_t>(fd), static_cast<uint64_t>(offset), origin);
}
static inline void put(unsigned char *out, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) out[i] = static_cast<unsigned char>(value >> (8 * i));
}
static inline uint64_t get(const unsigned char *in, unsigned count) {
    uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value |= uint64_t(in[i]) << (8 * i); return value;
}
static inline int ctl(Context &c, int ep, int operation, int fd, uint32_t mask, uint64_t cookie) {
    unsigned char bytes[16]{}; put(bytes, mask, 4); put(bytes + 8, cookie, 8);
    CHECK(artbox_vm_write(c.thread.vm, c.scratch + 32, bytes, sizeof(bytes)) == 0);
    return static_cast<int>(call(c, 21, ep, operation, fd, c.scratch + 32));
}
static inline int poll(Context &c, int ep, uint64_t *cookies, int maximum = 1, int timeout = 0) {
    int result = static_cast<int>(call(c, 22, ep, c.scratch + 64, maximum, static_cast<uint64_t>(timeout), 0, 8));
    unsigned char bytes[32]{};
    CHECK(artbox_vm_read(c.thread.vm, c.scratch + 64, bytes, sizeof(bytes)) == 0);
    if (result > 0) for (int i = 0; i < result; ++i) cookies[i] = get(bytes + i * 16 + 8, 8);
    return result;
}
static inline int snapshot(void *opaque, int fd, unsigned events) {
    auto &c = *static_cast<Context *>(opaque);
    int ep = static_cast<int>(call(c, 20)); CHECK(ep >= 3);
    int result = ctl(c, ep, 1, fd, events, UINT64_C(0xfedc12345678abcd));
    if (!result) {
        uint64_t cookie = 0; result = poll(c, ep, &cookie);
        if (result > 0) {
            CHECK(result == 1 && cookie == UINT64_C(0xfedc12345678abcd));
            unsigned char bytes[4]; CHECK(artbox_vm_read(c.thread.vm, c.scratch + 64, bytes, sizeof(bytes)) == 0);
            result = static_cast<int>(get(bytes, 4));
        }
    }
    CHECK(close_fd(&c, ep) == 0); return result;
}
#endif
