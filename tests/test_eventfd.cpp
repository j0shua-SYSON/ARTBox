// Original guest eventfd ownership and concurrency controls. SPDX-License-Identifier: MIT
#include "artbox/vfs.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include "artbox/native_wake.h"
#include "artbox/signals.h"
#include "../fixtures/eventfd/check.h"
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
static int create_wake(void *context, void **output) {
    auto *probe = static_cast<Probe *>(context); *output = nullptr;
    if (probe->fail_create.exchange(false)) return -12;
    auto *owner = new (std::nothrow) Owner{probe, nullptr};
    if (!owner) return -12;
    int error = probe->native.create(probe->native.context, &owner->native);
    if (error) { delete owner; return error; }
    ++probe->live; *output = owner; return 0;
}
static int signal_wake(void *opaque) {
    auto *owner = static_cast<Owner *>(opaque); return owner->probe->native.signal(owner->native);
}
static int wait_wake(void *opaque, int timeout) {
    auto *owner = static_cast<Owner *>(opaque);
    if (owner->probe->fail_wait.exchange(false)) return -5;
    ++owner->probe->waiting;
    int result = owner->probe->native.wait(owner->native, timeout);
    --owner->probe->waiting; return result;
}
static int close_wake(void *opaque) {
    auto *owner = static_cast<Owner *>(opaque);
    int result = owner->probe->native.close(owner->native);
    --owner->probe->live; delete owner; return result;
}
static void await(std::atomic<unsigned> &value, unsigned target) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (value.load() < target && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    CHECK(value.load() >= target);
}
struct Context { artbox_vfs *fs; artbox_kernel_thread thread; uint64_t scratch; };
static int64_t call(Context &c, uint64_t n, uint64_t a = 0, uint64_t b = 0, uint64_t d = 0,
                    uint64_t e = 0, uint64_t f = 0, uint64_t g = 0) {
    return artbox_vfs_syscall(c.fs, &c.thread, n, a, b, d, e, f, g);
}
static int create(void *opaque, uint64_t initial, uint64_t flags) {
    return static_cast<int>(call(*static_cast<Context *>(opaque), 19, initial, flags));
}
static int close_fd(void *opaque, int fd) {
    return static_cast<int>(call(*static_cast<Context *>(opaque), 57, static_cast<uint64_t>(fd)));
}
static int64_t read_fd(void *opaque, int fd, void *output, size_t size) {
    auto &c = *static_cast<Context *>(opaque);
    if (output) CHECK(artbox_vm_write(c.thread.vm, c.scratch, output, 16) == 0);
    int64_t result = call(c, 63, static_cast<uint64_t>(fd), output ? c.scratch : 0, size);
    if (output) CHECK(artbox_vm_read(c.thread.vm, c.scratch, output, 16) == 0);
    return result;
}
static int64_t write_fd(void *opaque, int fd, const void *input, size_t size) {
    auto &c = *static_cast<Context *>(opaque);
    if (input) CHECK(artbox_vm_write(c.thread.vm, c.scratch, input, 16) == 0);
    return call(c, 64, static_cast<uint64_t>(fd), input ? c.scratch : 0, size);
}
static int64_t seek_fd(void *opaque, int fd, int64_t offset, unsigned origin) {
    return call(*static_cast<Context *>(opaque), 62, static_cast<uint64_t>(fd), static_cast<uint64_t>(offset), origin);
}
static void put(unsigned char *out, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) out[i] = static_cast<unsigned char>(value >> (8 * i));
}
static uint64_t get(const unsigned char *in, unsigned count) {
    uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value |= uint64_t(in[i]) << (8 * i); return value;
}
static int ctl(Context &c, int ep, int operation, int fd, uint32_t mask, uint64_t cookie) {
    unsigned char bytes[16]{}; put(bytes, mask, 4); put(bytes + 8, cookie, 8);
    CHECK(artbox_vm_write(c.thread.vm, c.scratch + 32, bytes, sizeof(bytes)) == 0);
    return static_cast<int>(call(c, 21, ep, operation, fd, c.scratch + 32));
}
static int poll(Context &c, int ep, uint64_t *cookies, int maximum = 1, int timeout = 0) {
    int result = static_cast<int>(call(c, 22, ep, c.scratch + 64, maximum, static_cast<uint64_t>(timeout), 0, 8));
    unsigned char bytes[32]{};
    CHECK(artbox_vm_read(c.thread.vm, c.scratch + 64, bytes, sizeof(bytes)) == 0);
    if (result > 0) for (int i = 0; i < result; ++i) cookies[i] = get(bytes + i * 16 + 8, 8);
    return result;
}
static int snapshot(void *opaque, int fd, unsigned events) {
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
static void wake_checks(Context &c, Probe &probe) {
    for (int kind = 0; kind < 3; ++kind) {
        int fd = create(&c, 0, 0); CHECK(fd >= 3);
        uint64_t value[2] = {UINT64_MAX - 1, 0};
        if (kind == 1) CHECK(write_fd(&c, fd, value, 8) == 8);
        int ep = -1;
        if (kind == 2) { ep = static_cast<int>(call(c, 20)); CHECK(ep >= 3 && ctl(c, ep, 1, fd, 1, 123) == 0); }
        Context worker = c; ++worker.thread.tid; worker.scratch += 256;
        std::atomic<unsigned> done{0}; int64_t result = -99;
        uint64_t output[2] = {7, 0};
        std::thread waiter([&] {
            result = kind == 0 ? read_fd(&worker, fd, output, 8) : kind == 1 ? write_fd(&worker, fd, output, 8) : poll(worker, ep, output, 1, -1);
            done = 1;
        });
        await(probe.waiting, 1); // Entered the native wake provider; not Linux syscall observation.
        if (kind == 1) CHECK(read_fd(&c, fd, value, 8) == 8 && value[0] == UINT64_MAX - 1);
        else { value[0] = 5; CHECK(write_fd(&c, fd, value, 8) == 8); }
        await(done, 1); waiter.join(); CHECK(probe.live == 0);
        if (kind == 0) CHECK(result == 8 && output[0] == 5);
        else if (kind == 1) CHECK(result == 8 && read_fd(&c, fd, value, 8) == 8 && value[0] == 7);
        else CHECK(result == 1 && output[0] == 123 && close_fd(&c, ep) == 0);
        CHECK(close_fd(&c, fd) == 0);
    }
}
static void notice(void *context) { ++*static_cast<std::atomic<unsigned> *>(context); }
static void interruption_and_close(Context &c, Probe &probe) {
    for (int writing = 0; writing < 2; ++writing) {
        auto *signals = artbox_signals_create(c.thread.vm, c.thread.pid, 10000, 1);
        CHECK(signals && artbox_signals_enable_interrupt(signals, 34, 4) == 0 && artbox_signals_attach(signals, &c.thread) == 0);
        std::atomic<unsigned> notices{0}, done{0};
        CHECK(artbox_signals_bind_interrupt(&c.thread, notice, &notices) == 0);
        int fd = create(&c, 0, 0), ep = static_cast<int>(call(c, 20)); CHECK(fd >= 3 && ep >= 3);
        uint64_t value[2] = {UINT64_MAX - 1, 0};
        if (writing) CHECK(write_fd(&c, fd, value, 8) == 8);
        CHECK(ctl(c, ep, 1, fd, writing ? 1 : 4, 111) == 0);
        int64_t result = -99; uint64_t io[2] = {1, 0};
        std::thread waiter([&] { result = writing ? write_fd(&c, fd, io, 8) : read_fd(&c, fd, io, 8); done = 1; });
        await(probe.waiting, 1);
        CHECK(close_fd(&c, fd) == 0);
        Context control = c; control.scratch += 256;
        int replacement = create(&control, 2, 0x80800); CHECK(replacement == fd);
        CHECK(ctl(control, ep, 3, replacement, 5, 222) == -2);
        CHECK(ctl(control, ep, 1, replacement, 5, 222) == 0);
        uint64_t cookies[2]{}; CHECK(poll(control, ep, cookies, 2) == 2);
        CHECK((cookies[0] == 111 && cookies[1] == 222) || (cookies[0] == 222 && cookies[1] == 111));
        CHECK(artbox_signals_call(&c.thread, 131, c.thread.pid, c.thread.tid, 34, 0) == 0);
        uint64_t mask = 0; CHECK(artbox_signals_take_interrupt(&c.thread, 0, &mask) == 34);
        CHECK(artbox_vm_write(c.thread.vm, control.scratch + 128, &mask, 8) == 0);
        CHECK(artbox_signals_call(&c.thread, 135, 2, control.scratch + 128, 0, 8) == 0);
        await(done, 1); waiter.join(); CHECK(result == -4 && notices == 1 && probe.live == 0);
        CHECK(poll(control, ep, cookies, 2) == 1 && cookies[0] == 222);
        CHECK(read_fd(&control, replacement, value, 8) == 8 && value[0] == 2);
        CHECK(close_fd(&control, replacement) == 0 && poll(control, ep, cookies, 2) == 0);
        CHECK(close_fd(&control, ep) == 0);
        CHECK(artbox_signals_bind_interrupt(&c.thread, nullptr, nullptr) == 0);
        CHECK(artbox_signals_detach(&c.thread) == 0 && artbox_signals_destroy(signals) == 0);
    }
}
static void admission_and_semaphore(Context &c, Probe &probe) {
    const int fd = create(&c, 0, 1); CHECK(fd >= 3);
    Context readers[2] = {c, c}; uint64_t values[2][2]{}; int64_t results[2]{};
    std::atomic<unsigned> done{0};
    for (unsigned i = 0; i < 2; ++i) { readers[i].thread.tid += static_cast<int>(i + 1); readers[i].scratch += (i + 1) * 256; }
    std::thread first([&] { results[0] = read_fd(&readers[0], fd, values[0], 8); ++done; });
    std::thread second([&] { results[1] = read_fd(&readers[1], fd, values[1], 8); ++done; });
    await(probe.waiting, 2);
    uint64_t value[2]{}; CHECK(read_fd(&c, fd, value, 8) == -12);
    // Epoll and blocking counter I/O share the same configured wait admission.
    int ep = static_cast<int>(call(c, 20)); CHECK(ep >= 3);
    CHECK(poll(c, ep, value, 1, -1) == -12 && close_fd(&c, ep) == 0);
    value[0] = 2; CHECK(write_fd(&c, fd, value, 8) == 8);
    await(done, 2); first.join(); second.join();
    CHECK(results[0] == 8 && results[1] == 8 && values[0][0] == 1 && values[1][0] == 1);
    CHECK(probe.live == 0 && close_fd(&c, fd) == 0);
}
static void faults_and_owners(Context &c, const artbox_vm_ops &memory) {
    const size_t page = memory.page_size;
    int fd = create(&c, 13, 0x80800); CHECK(fd >= 3);
    CHECK(artbox_vm_mprotect(c.thread.vm, c.scratch + page, page, 0) == 0);
    CHECK(call(c, 63, fd, c.scratch + page - 4, 8) == -14);
    uint64_t value[2]{}; CHECK(read_fd(&c, fd, value, 8) == -11);
    CHECK(call(c, 64, fd, c.scratch + page - 4, 8) == -14);
    CHECK(read_fd(&c, fd, value, 8) == -11);
    CHECK(artbox_vm_mprotect(c.thread.vm, c.scratch + page, page, 3) == 0);
    auto *foreign = artbox_vm_create(&memory, page, 1); CHECK(foreign);
    Context other = c; other.thread.vm = foreign;
    CHECK(call(other, 63, fd, 0, 8) == -95 && call(other, 64, fd, 0, 8) == -95);
    CHECK(artbox_vfs_events(c.fs, &other.thread, fd) == -95);
    int ep = static_cast<int>(call(other, 20)); CHECK(ep >= 3);
    CHECK(call(other, 21, ep, 1, fd, 0) == -95);
    CHECK(close_fd(&other, ep) == 0 && artbox_vm_destroy(foreign) == 0);
    CHECK(close_fd(&c, fd) == 0);
}
int main(void) {
    auto memory = artbox_native_vm(); auto system = artbox_native_system();
    auto *vm = artbox_vm_create(&memory, memory.page_size * 4, 8); CHECK(vm);
    int64_t address = artbox_vm_mmap(vm, 0, memory.page_size * 2, 3, 0x22, -1, 0); CHECK(address > 0);
    Context c{}; c.fs = artbox_vfs_create(nullptr, 8); c.scratch = static_cast<uint64_t>(address);
    CHECK(c.fs && artbox_kernel_thread_init(&c.thread, vm, &system, 100, 100) == 0);
    CHECK(create(&c, 0, 0) == -38);
    Probe probe; artbox_wake_ops wake{&probe, create_wake, signal_wake, wait_wake, close_wake};
    CHECK(artbox_vfs_set_epoll(c.fs, &wake, 8, 2) == 0);
    const artbox_eventfd_ops ops{create, close_fd, read_fd, write_fd, snapshot, seek_fd};
    CHECK(artbox_eventfd_check(&c, &ops) == 63);
    wake_checks(c, probe); interruption_and_close(c, probe);
    admission_and_semaphore(c, probe); faults_and_owners(c, memory);
    int fd = create(&c, 0, 0); CHECK(fd >= 3);
    uint64_t value[2]{};
    probe.fail_create = true; CHECK(read_fd(&c, fd, value, 8) == -12 && probe.live == 0);
    probe.fail_wait = true; CHECK(read_fd(&c, fd, value, 8) == -5 && probe.live == 0);
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i) writers.emplace_back([&, i] {
        Context writer = c; writer.thread.tid += i + 1; writer.scratch += static_cast<uint64_t>(i + 1) * 256;
        uint64_t increment[2] = {1, 0};
        for (unsigned j = 0; j < 512; ++j) CHECK(write_fd(&writer, fd, increment, 8) == 8);
    });
    for (auto &writer : writers) writer.join();
    CHECK(read_fd(&c, fd, value, 8) == 8 && value[0] == 2048);
    CHECK(call(c, 80, fd, c.scratch) == -95 && call(c, 29, fd, 0, 0) == -25);
    CHECK(artbox_vfs_mmap(c.fs, vm, 0, memory.page_size, 1, 1, fd, 0) == -19);
    CHECK(close_fd(&c, fd) == 0);
    std::vector<int> opened;
    for (int i = 0; i < 8; ++i) { fd = create(&c, 0, 0x80800); CHECK(fd >= 3); opened.push_back(fd); }
    CHECK(create(&c, 0, 0) == -24);
    for (int item : opened) CHECK(close_fd(&c, item) == 0);
    CHECK(probe.live == 0 && artbox_vfs_destroy(c.fs) == 0 && artbox_vm_destroy(vm) == 0);
    std::puts("{\"shared_cases\":63,\"wake_cases\":3,\"close_interrupt_cases\":2,\"concurrent_writes\":2048,\"copy_faults\":true,\"wait_admission\":true,\"passed\":true}");
    return 0;
}
