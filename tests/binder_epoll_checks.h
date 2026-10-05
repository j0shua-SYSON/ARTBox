/* Original VFS epoll controls, included after the receive test's VM/provider
 * helpers. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_TEST_BINDER_EPOLL_CHECKS_H
#define ARTBOX_TEST_BINDER_EPOLL_CHECKS_H
#include <new>

struct WakeProbe {
    artbox_wake_ops native = artbox_native_wake();
    std::atomic<unsigned> live{0}, waiting{0};
    std::atomic<bool> fail_create{false}, fail_wait{false};
};
struct ProbedWake { WakeProbe *probe; void *native; };
static int probe_create(void *context, void **output) {
    auto *probe = static_cast<WakeProbe *>(context);
    *output = nullptr;
    if (probe->fail_create.exchange(false)) return -12;
    auto *owner = new (std::nothrow) ProbedWake{probe, nullptr};
    if (!owner) return -12;
    int result = probe->native.create(probe->native.context, &owner->native);
    if (result) { delete owner; return result; }
    ++probe->live; *output = owner; return 0;
}
static int probe_signal(void *opaque) {
    auto *owner = static_cast<ProbedWake *>(opaque);
    return owner->probe->native.signal(owner->native);
}
static int probe_wait(void *opaque, int timeout) {
    auto *owner = static_cast<ProbedWake *>(opaque);
    if (owner->probe->fail_wait.exchange(false)) return -5;
    ++owner->probe->waiting;
    int result = owner->probe->native.wait(owner->native, timeout);
    --owner->probe->waiting;
    return result;
}
static int probe_close(void *opaque) {
    auto *owner = static_cast<ProbedWake *>(opaque);
    int result = owner->probe->native.close(owner->native);
    --owner->probe->live; delete owner; return result;
}
static int64_t ep_call(Context &c, uint64_t number, uint64_t a0 = 0, uint64_t a1 = 0,
    uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0, uint64_t a5 = 0) {
    return artbox_vfs_syscall(c.fs, &c.thread, number, a0, a1, a2, a3, a4, a5);
}
static void ep_encode(unsigned char *out, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out[i] = static_cast<unsigned char>(value >> (i * 8));
}
static uint64_t ep_decode(const unsigned char *in, unsigned width) {
    uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i) result |= uint64_t(in[i]) << (i * 8);
    return result;
}
struct EpEvent { uint32_t events; uint64_t data; };
static int ep_ctl(Context &c, int ep, int operation, int fd, uint32_t events, uint64_t data) {
    unsigned char bytes[16]; std::memset(bytes, 0xa5, sizeof(bytes));
    ep_encode(bytes, events, 4); ep_encode(bytes + 8, data, 8);
    CHECK(artbox_vm_write(c.thread.vm, c.path + 3072, bytes, sizeof(bytes)) == 0);
    return static_cast<int>(ep_call(c, 21, ep, operation, fd, operation == 2 ? 0 : c.path + 3072));
}
static int ep_wait(Context &c, int ep, EpEvent *events, unsigned maximum, int timeout = 0) {
    CHECK(maximum && maximum <= 2);
    unsigned char bytes[64]; std::memset(bytes, 0xa5, sizeof(bytes));
    CHECK(artbox_vm_write(c.thread.vm, c.path + 3200, bytes, sizeof(bytes)) == 0);
    int result = static_cast<int>(ep_call(c, 22, ep, c.path + 3216, maximum, static_cast<uint64_t>(timeout), 0, 8));
    CHECK(artbox_vm_read(c.thread.vm, c.path + 3200, bytes, sizeof(bytes)) == 0);
    for (unsigned i = 0; i < 16; ++i) CHECK(bytes[i] == 0xa5);
    for (unsigned i = 16 + maximum * 16; i < sizeof(bytes); ++i) CHECK(bytes[i] == 0xa5);
    if (result > 0) {
        CHECK(static_cast<unsigned>(result) <= maximum);
        for (int i = 0; i < result; ++i) {
            events[i].events = static_cast<uint32_t>(ep_decode(bytes + 16 + i * 16, 4));
            events[i].data = ep_decode(bytes + 24 + i * 16, 8);
        }
    } else for (unsigned i = 16; i < 16 + maximum * 16; ++i) CHECK(bytes[i] == 0xa5);
    return result;
}
struct EpInterest { int fd = -1, ep = -1; };
struct EpContext { Context *base; EpInterest interests[4]; };
static EpInterest *ep_interest(EpContext *c, int fd) {
    for (auto &interest : c->interests) if (interest.fd == fd) return &interest;
    return nullptr;
}
static uint64_t ep_cookie(int fd) { return uint64_t(static_cast<unsigned>(fd)) << 32 | UINT64_C(0xfaceabcd); }
static int ep_open(void *opaque) {
    auto *c = static_cast<EpContext *>(opaque);
    auto *interest = ep_interest(c, -1); CHECK(interest);
    int fd = open_device(c->base); CHECK(fd >= 3);
    int ep = static_cast<int>(ep_call(*c->base, 20, 0x80000)); CHECK(ep >= 3);
    CHECK(ep_ctl(*c->base, ep, 1, fd, 5, ep_cookie(fd)) == 0);
    CHECK(ep_ctl(*c->base, ep, 1, fd, 5, ep_cookie(fd)) == -17);
    interest->fd = fd; interest->ep = ep; return fd;
}
static int ep_close(void *opaque, int fd) {
    auto *c = static_cast<EpContext *>(opaque);
    auto *interest = ep_interest(c, fd); CHECK(interest);
    CHECK(close_device(c->base, fd) == 0);
    EpEvent event{}; CHECK(ep_wait(*c->base, interest->ep, &event, 1) == 0);
    CHECK(close_device(c->base, interest->ep) == 0);
    *interest = EpInterest{}; return 0;
}
static int64_t ep_ioctl(void *opaque, int fd, uint32_t request, uint64_t argument) {
    return ioctl_device(static_cast<EpContext *>(opaque)->base, fd, request, argument);
}
static int ep_snapshot(void *opaque, int fd, unsigned events) {
    auto *c = static_cast<EpContext *>(opaque);
    auto *interest = ep_interest(c, fd); CHECK(interest);
    CHECK(ep_ctl(*c->base, interest->ep, 3, fd, events, ep_cookie(fd)) == 0);
    EpEvent event{};
    int result = ep_wait(*c->base, interest->ep, &event, 1);
    if (result <= 0) return result;
    CHECK(result == 1 && event.data == ep_cookie(fd));
    return static_cast<int>(event.events);
}
static void ep_await(const std::atomic<unsigned> &value, unsigned minimum) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (value.load() < minimum) { CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
}
static void ep_concurrent_waits(Context &c, WakeProbe &probe) {
    int ep = static_cast<int>(ep_call(c, 20)), fd = open_device(&c); CHECK(ep >= 3 && fd >= 3);
    Context clients[2] = {c, c};
    EpEvent events[2]{};
    int results[2] = {-99, -99};
    std::atomic<unsigned> done{0};
    for (unsigned i = 0; i < 2; ++i) { clients[i].thread.tid += static_cast<int32_t>(i + 1); clients[i].path += (i + 1) * 128; }
    std::thread first([&] { results[0] = ep_wait(clients[0], ep, &events[0], 1, -1); ++done; });
    std::thread second([&] { results[1] = ep_wait(clients[1], ep, &events[1], 1, -1); ++done; });
    ep_await(probe.waiting, 2);
    EpEvent unused{};
    CHECK(ep_wait(c, ep, &unused, 1, -1) == -12); // Bounded concurrent wait admission.
    const uint64_t cookie = UINT64_C(0xf00123456789abcd);
    CHECK(ep_ctl(c, ep, 1, fd, 1, cookie) == 0); // Publish to both distinct native wait owners.
    ep_await(done, 2);
    first.join(); second.join();
    for (unsigned i = 0; i < 2; ++i) CHECK(results[i] == 1 && events[i].events == 1 && events[i].data == cookie);
    CHECK(probe.live == 0 && close_device(&c, fd) == 0 && close_device(&c, ep) == 0);
}
static void ep_closed_wait(Context &c, WakeProbe &probe) {
    int owner = open_device(&c), fd = open_device(&c), ep = static_cast<int>(ep_call(c, 20));
    CHECK(owner >= 3 && fd >= 3 && ep >= 3);
    CHECK(ioctl_device(&c, owner, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    uint32_t commands[6] = {ARTBOX_BC_ACQUIRE, 0, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, 0, 42, 0};
    uint64_t header[6] = {sizeof(commands), 0, c.path + 256, 0, 0, 0};
    CHECK(artbox_vm_write(c.thread.vm, c.path + 128, header, sizeof(header)) == 0);
    CHECK(artbox_vm_write(c.thread.vm, c.path + 256, commands, sizeof(commands)) == 0);
    CHECK(ioctl_device(&c, fd, ARTBOX_BINDER_WRITE_READ, c.path + 128) == 0);
    CHECK(ep_ctl(c, ep, 1, fd, 1, UINT64_C(0x01234567deadbeef)) == 0);
    Context client = c; ++client.thread.tid;
    CHECK(ioctl_device(&client, fd, ARTBOX_BINDER_VERSION, c.path + 768) == 0);
    EpEvent event{};
    std::atomic<unsigned> done{0}; int result = -99;
    std::thread waiter([&] { result = ep_wait(client, ep, &event, 1, -2); done = 1; });
    ep_await(probe.waiting, 1);
    CHECK(close_device(&c, ep) == 0);
    int replacement = static_cast<int>(ep_call(c, 20)); CHECK(replacement == ep);
    CHECK(close_device(&c, owner) == 0);
    ep_await(done, 1); waiter.join();
    CHECK(result == 1 && event.events == 1 && event.data == UINT64_C(0x01234567deadbeef));
    CHECK(ep_wait(c, replacement, &event, 1) == 0); // No event leaked to the reused epoll fd.
    CHECK(close_device(&c, fd) == 0 && close_device(&c, replacement) == 0 && probe.live == 0);
}
static void ep_interruption(Context &c, WakeProbe &probe) {
    auto *signals = artbox_signals_create(c.thread.vm, c.thread.pid, 10000, 1);
    CHECK(signals && artbox_signals_enable_interrupt(signals, 34, 4) == 0);
    CHECK(artbox_signals_attach(signals, &c.thread) == 0);
    std::atomic<unsigned> notices{0}, done{0};
    CHECK(artbox_signals_bind_interrupt(&c.thread, interrupt_notice, &notices) == 0);
    int ep = static_cast<int>(ep_call(c, 20)); CHECK(ep >= 3);
    EpEvent event{}; int result = -99;
    std::thread waiter([&] { result = ep_wait(c, ep, &event, 1, -1); done = 1; });
    ep_await(probe.waiting, 1);
    CHECK(artbox_signals_call(&c.thread, 131, c.thread.pid, c.thread.tid, 34, 0) == 0);
    uint64_t mask = 0;
    CHECK(artbox_signals_take_interrupt(&c.thread, 0, &mask) == 34); // Injected ordinary-context delivery.
    CHECK(artbox_vm_write(c.thread.vm, c.path + 1024, &mask, sizeof(mask)) == 0);
    CHECK(artbox_signals_call(&c.thread, 135, 2, c.path + 1024, 0, 8) == 0);
    ep_await(done, 1); waiter.join();
    CHECK(result == -4 && notices == 1 && probe.live == 0);
    CHECK(close_device(&c, ep) == 0);
    CHECK(artbox_signals_bind_interrupt(&c.thread, nullptr, nullptr) == 0);
    CHECK(artbox_signals_detach(&c.thread) == 0 && artbox_signals_destroy(signals) == 0);
}
static void ep_deadline(Context &c) {
    const int ep = static_cast<int>(ep_call(c, 20)), fd = open_device(&c); CHECK(ep >= 3 && fd >= 3);
    Context writer_context = c; ++writer_context.thread.tid;
    std::atomic<unsigned> mutations{0}; std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load()) {
            CHECK(ioctl_device(&writer_context, fd, ARTBOX_BINDER_VERSION, c.path + 768) == 0);
            ++mutations; std::this_thread::yield();
        }
    });
    ep_await(mutations, 10);
    const auto start = std::chrono::steady_clock::now();
    EpEvent event{};
    CHECK(ep_wait(c, ep, &event, 1, 20) == 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    stop = true; writer.join();
    CHECK(elapsed >= std::chrono::milliseconds(10) && elapsed < std::chrono::seconds(2));
    CHECK(close_device(&c, fd) == 0 && close_device(&c, ep) == 0);
}
static void ep_ignore_hint(void *) {}
static void ep_configuration(artbox_binder_device *device, const artbox_wake_ops &wake) {
    auto *first = artbox_vfs_create(nullptr, 2), *second = artbox_vfs_create(nullptr, 2);
    CHECK(first && second);
    CHECK(artbox_vfs_set_epoll(nullptr, &wake, 1, 1) == -22);
    CHECK(artbox_vfs_set_epoll(first, nullptr, 1, 1) == -22);
    CHECK(artbox_vfs_set_epoll(first, &wake, 0, 1) == -22);
    CHECK(artbox_vfs_set_epoll(first, &wake, 1, 0) == -22);
    auto missing = wake; missing.signal = nullptr;
    CHECK(artbox_vfs_set_epoll(first, &missing, 1, 1) == -22);
    CHECK(artbox_vfs_set_binder(first, device, 10000) == 0);
    CHECK(artbox_vfs_set_epoll(second, &wake, 1, 1) == 0);
    uint64_t subscriptions[64]{};
    for (auto &subscription : subscriptions)
        CHECK(artbox_binder_device_observe(device, ep_ignore_hint, nullptr, &subscription) == 0);
    CHECK(artbox_vfs_set_epoll(first, &wake, 1, 1) == -28);
    CHECK(artbox_vfs_set_binder(second, device, 10000) == -28);
    for (auto subscription : subscriptions) CHECK(artbox_binder_device_unobserve(device, subscription) == 0);
    CHECK(artbox_vfs_set_epoll(first, &wake, 1, 1) == 0 && artbox_vfs_set_binder(second, device, 10000) == 0);
    CHECK(artbox_vfs_destroy(first) == 0 && artbox_vfs_destroy(second) == 0);
}
static void ep_owner_and_errors(Context &c, artbox_binder_device *device, const artbox_wake_ops &wake) {
    int ep = static_cast<int>(ep_call(c, 20)), fd = open_device(&c); CHECK(ep >= 3 && fd >= 3);
    CHECK(ep_call(c, 21, -1, 1, fd, c.path + 3072) == -9);
    CHECK(ep_call(c, 21, fd, 1, ep, c.path + 3072) == -22);
    CHECK(ep_ctl(c, ep, 1, -1, 1, 0) == -9);
    CHECK(ep_ctl(c, ep, 1, fd, 0, 123) == 0); // POLLERR is reported despite an empty interest mask.
    for (int i = 1; i <= 3; ++i) {
        Context admitted = c; admitted.thread.tid += i;
        CHECK(ioctl_device(&admitted, fd, ARTBOX_BINDER_VERSION, c.path + 768) == 0);
    }
    Context excess = c; excess.thread.tid += 4;
    EpEvent event{};
    CHECK(ep_wait(excess, ep, &event, 1) == 1 && event.events == 8 && event.data == 123);
    auto memory = artbox_native_vm();
    auto *foreign_vm = artbox_vm_create(&memory, memory.page_size, 1); CHECK(foreign_vm);
    Context foreign = c; foreign.thread.vm = foreign_vm;
    CHECK(ep_call(foreign, 22, ep, 0, 1, 0) == -95);
    CHECK(ep_call(foreign, 21, ep, 2, fd, 0) == -95);
    int other = static_cast<int>(ep_call(foreign, 20)); CHECK(other >= 3);
    CHECK(ep_call(foreign, 21, other, 1, fd, 0) == -95);
    CHECK(close_device(&foreign, other) == 0 && artbox_vm_destroy(foreign_vm) == 0);
    CHECK(close_device(&c, fd) == 0 && close_device(&c, ep) == 0);

    Context limited = c; limited.fs = artbox_vfs_create(nullptr, 1); CHECK(limited.fs);
    CHECK(ep_call(limited, 20) == -38);
    CHECK(artbox_vfs_set_binder(limited.fs, device, 10000) == 0);
    CHECK(artbox_vfs_set_epoll(limited.fs, &wake, 1, 1) == 0);
    ep = static_cast<int>(ep_call(limited, 20)); CHECK(ep == 3);
    CHECK(ep_call(limited, 20) == -24);
    CHECK(close_device(&limited, ep) == 0 && ep_call(limited, 20) == ep);
    CHECK(artbox_vfs_destroy(limited.fs) == 0);
}
static void guest_epoll_checks(Context &original, artbox_binder_device *device, size_t page) {
    Context c = original; c.fs = artbox_vfs_create(nullptr, 16); CHECK(c.fs);
    WakeProbe probe;
    const artbox_wake_ops wake = {&probe, probe_create, probe_signal, probe_wait, probe_close};
    CHECK(artbox_vfs_set_epoll(c.fs, &wake, 2, 2) == 0);
    CHECK(artbox_vfs_set_epoll(c.fs, &wake, 2, 2) == -114);
    CHECK(artbox_vfs_set_binder(c.fs, device, 10000) == 0);
    EpContext context{&c, {}};
    const artbox_binder_device_ops ops = {ep_open, ep_close, ep_ioctl, pause_device};
    CHECK(artbox_binder_poll_check(&context, &ops, ep_snapshot,
        reinterpret_cast<artbox_binder_poll_scratch *>(c.path + 128)) == 31);
    CHECK(ep_call(c, 20, 1) == -22);
    int ep = static_cast<int>(ep_call(c, 20)), fd = open_device(&c); CHECK(ep >= 3 && fd >= 3);
    EpEvent events[2]{};
    CHECK(ep_ctl(c, ep, 2, fd, 0, 0) == -2 && ep_ctl(c, ep, 3, fd, 1, 0) == -2);
    CHECK(ep_ctl(c, ep, 1, ep, 1, 0) == -22);
    CHECK(ep_call(c, 21, ep, 1, fd, 0) == -14);
    CHECK(ep_ctl(c, ep, 1, fd, UINT32_C(0x80000001), 1) == -95);
    CHECK(ep_ctl(c, ep, 1, fd, 1, UINT64_C(0x123456789abcdef0)) == 0);
    CHECK(ep_call(c, 22, ep, 0, 1, 0) == -14);
    CHECK(ep_wait(c, ep, events, 1) == 1 && events[0].events == 1 && events[0].data == UINT64_C(0x123456789abcdef0));
    CHECK(ep_call(c, 22, ep, c.path + 3216, 0, 0) == -22);
    CHECK(ep_call(c, 22, ep, c.path + 3216, 1, 0, c.path + 3072, 16) == -22);
    CHECK(ep_call(c, 22, ep, c.path + 3216, 1, 0, c.path + 3072, 8) == -95);
    CHECK(ep_call(c, 63, ep, c.path + 3216, 1) == -22);
    CHECK(ep_call(c, 64, ep, c.path + 3216, 1) == -22);
    CHECK(ep_call(c, 80, ep, c.path + 3216) == -95 && ep_call(c, 62, ep, 0, 0) == -95);
    CHECK(artbox_vfs_mmap(c.fs, c.thread.vm, 0, page, 1, 2, ep, 0) == -19);

    int64_t mapped = map_device(&c, fd, page * 2, 1, 2, 0); CHECK(mapped > 0);
    CHECK(close_device(&c, fd) == 0 && ep_wait(c, ep, events, 1) == 1);
    int reused = open_device(&c); CHECK(reused == fd);
    CHECK(ep_ctl(c, ep, 3, reused, 1, 2) == -2 && ep_ctl(c, ep, 1, reused, 1, 2) == 0);
    CHECK(ep_wait(c, ep, events, 2) == 2 && events[0].data != events[1].data);
    CHECK((events[0].data == 2 && events[1].data == UINT64_C(0x123456789abcdef0)) ||
          (events[1].data == 2 && events[0].data == UINT64_C(0x123456789abcdef0)));
    CHECK(ep_wait(c, ep, events, 1) == 1);
    const uint64_t first = events[0].data;
    CHECK(ep_wait(c, ep, events, 1) == 1 && events[0].data != first); // Fair level-triggered scan.
    int64_t output = artbox_vm_mmap(c.thread.vm, 0, page * 2, 3, 0x22, -1, 0); CHECK(output > 0);
    CHECK(artbox_vm_munmap(c.thread.vm, static_cast<uint64_t>(output) + page, page) == 0);
    CHECK(ep_call(c, 22, ep, static_cast<uint64_t>(output) + page - 16, 2, 0) == 1); // Partial copy returns prior events.
    CHECK(artbox_vm_munmap(c.thread.vm, static_cast<uint64_t>(output), page) == 0);
    CHECK(ep_wait(c, ep, events, 2) == 2); // A copy fault did not consume readiness.
    int extra = open_device(&c); CHECK(extra >= 3);
    CHECK(ep_ctl(c, ep, 1, extra, 1, 3) == -28);
    CHECK(ep_ctl(c, ep, 2, reused, 0, 0) == 0);
    CHECK(close_device(&c, reused) == 0 && close_device(&c, extra) == 0);
    CHECK(unmap_device(&c, static_cast<uint64_t>(mapped), page) == 0 && ep_wait(c, ep, events, 1) == 1);
    CHECK(unmap_device(&c, static_cast<uint64_t>(mapped) + page, page) == 0 && ep_wait(c, ep, events, 1) == 0);
    auto start = std::chrono::steady_clock::now();
    CHECK(ep_wait(c, ep, events, 1, 20) == 0);
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(10));
    probe.fail_create = true; CHECK(ep_wait(c, ep, events, 1, 1) == -12 && probe.live == 0);
    probe.fail_wait = true; CHECK(ep_wait(c, ep, events, 1, 20) == -5 && probe.live == 0);
    CHECK(close_device(&c, ep) == 0);
    ep_concurrent_waits(c, probe);
    ep_closed_wait(c, probe);
    ep_interruption(c, probe);
    ep_deadline(c);
    ep_owner_and_errors(c, device, wake);
    CHECK(artbox_vfs_destroy(c.fs) == 0 && probe.live == 0);
    ep_configuration(device, wake);
}
#endif
