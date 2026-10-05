// Original guest monotonic timer and ownership controls. SPDX-License-Identifier: MIT
#include "vfs_wait_helpers.h"
#include "../fixtures/timerfd/check.h"
static int create_timer(void *opaque, uint64_t clock, uint64_t flags) {
    return static_cast<int>(call(*static_cast<Context *>(opaque), 85, clock, flags));
}
static void encode_spec(unsigned char *bytes, const artbox_timer_spec &value) {
    put(bytes, static_cast<uint64_t>(value.interval_seconds), 8);
    put(bytes + 8, static_cast<uint64_t>(value.interval_nanoseconds), 8);
    put(bytes + 16, static_cast<uint64_t>(value.value_seconds), 8);
    put(bytes + 24, static_cast<uint64_t>(value.value_nanoseconds), 8);
}
static void decode_spec(const unsigned char *bytes, artbox_timer_spec &value) {
    value.interval_seconds = static_cast<int64_t>(get(bytes, 8));
    value.interval_nanoseconds = static_cast<int64_t>(get(bytes + 8, 8));
    value.value_seconds = static_cast<int64_t>(get(bytes + 16, 8));
    value.value_nanoseconds = static_cast<int64_t>(get(bytes + 24, 8));
}
static int set_timer(void *opaque, int fd, unsigned flags, const artbox_timer_spec *value, artbox_timer_spec *old) {
    auto &c = *static_cast<Context *>(opaque); unsigned char bytes[32]{};
    if (value) { encode_spec(bytes, *value); CHECK(artbox_vm_write(c.thread.vm, c.scratch + 128, bytes, sizeof(bytes)) == 0); }
    const bool valid_old = old && reinterpret_cast<uintptr_t>(old) != 1;
    uint64_t output = valid_old ? c.scratch + 160 : old ? 1 : 0;
    int result = static_cast<int>(call(c, 86, static_cast<uint64_t>(fd), flags, value ? c.scratch + 128 : 0, output));
    if (!result && valid_old) { CHECK(artbox_vm_read(c.thread.vm, output, bytes, sizeof(bytes)) == 0); decode_spec(bytes, *old); }
    return result;
}
static int get_timer(void *opaque, int fd, artbox_timer_spec *value) {
    auto &c = *static_cast<Context *>(opaque); unsigned char bytes[32]{};
    int result = static_cast<int>(call(c, 87, static_cast<uint64_t>(fd), value ? c.scratch + 128 : 0));
    if (!result && value) { CHECK(artbox_vm_read(c.thread.vm, c.scratch + 128, bytes, sizeof(bytes)) == 0); decode_spec(bytes, *value); }
    return result;
}
static int64_t clock_ns(void *opaque) {
    auto &c = *static_cast<Context *>(opaque); artbox_timespec now{};
    CHECK(c.thread.system.clock(1, &now) == 0); return now.seconds * 1000000000 + now.nanoseconds;
}
static void pause_test(void *) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
static void wake_checks(Context &c, Probe &probe) {
    for (int use_epoll = 0; use_epoll < 2; ++use_epoll) {
        int fd = create_timer(&c, 1, 0), ep = static_cast<int>(call(c, 20)); CHECK(fd >= 3 && ep >= 3);
        CHECK(ctl(c, ep, 1, fd, 1, 123) == 0);
        Context worker = c; worker.scratch += 256; ++worker.thread.tid;
        uint64_t value[2]{}; int64_t result = -99; std::atomic<unsigned> done{0};
        std::thread waiter([&] { result = use_epoll ? poll(worker, ep, value, 1, -1) : read_fd(&worker, fd, value, 8); done = 1; });
        await(probe.waiting, 1);
        artbox_timer_spec spec{0, 0, 0, 10000000}; CHECK(set_timer(&c, fd, 0, &spec, nullptr) == 0);
        await(done, 1); waiter.join();
        CHECK(result == (use_epoll ? 1 : 8) && value[0] == (use_epoll ? 123u : 1u));
        CHECK(probe.live == 0 && close_fd(&c, fd) == 0 && close_fd(&c, ep) == 0);
    }
}
static void notice(void *context) { ++*static_cast<std::atomic<unsigned> *>(context); }
static void closed_wait(Context &c, Probe &probe) {
    auto *signals = artbox_signals_create(c.thread.vm, c.thread.pid, 10000, 1);
    CHECK(signals && artbox_signals_enable_interrupt(signals, 34, 4) == 0 && artbox_signals_attach(signals, &c.thread) == 0);
    std::atomic<unsigned> notices{0}, done{0}; CHECK(artbox_signals_bind_interrupt(&c.thread, notice, &notices) == 0);
    int fd = create_timer(&c, 1, 0); CHECK(fd >= 3);
    uint64_t value[2]{}; int64_t result = -99;
    std::thread waiter([&] { result = read_fd(&c, fd, value, 8); done = 1; });
    await(probe.waiting, 1); CHECK(close_fd(&c, fd) == 0);
    Context control = c; control.scratch += 256;
    int replacement = create_timer(&control, 1, 0x80800); CHECK(replacement == fd);
    artbox_timer_spec spec{0, 0, 0, 1}; CHECK(set_timer(&control, replacement, 1, &spec, nullptr) == 0);
    uint64_t ticks[2]{}; CHECK(read_fd(&control, replacement, ticks, 8) == 8 && ticks[0] == 1);
    CHECK(artbox_signals_call(&c.thread, 131, c.thread.pid, c.thread.tid, 34, 0) == 0);
    uint64_t mask = 0; CHECK(artbox_signals_take_interrupt(&c.thread, 0, &mask) == 34);
    CHECK(artbox_vm_write(c.thread.vm, control.scratch + 224, &mask, 8) == 0);
    CHECK(artbox_signals_call(&c.thread, 135, 2, control.scratch + 224, 0, 8) == 0);
    await(done, 1); waiter.join(); CHECK(result == -4 && notices == 1 && probe.live == 0);
    CHECK(close_fd(&control, replacement) == 0);
    CHECK(artbox_signals_bind_interrupt(&c.thread, nullptr, nullptr) == 0);
    CHECK(artbox_signals_detach(&c.thread) == 0 && artbox_signals_destroy(signals) == 0);
}
static std::atomic<int64_t> controlled_time{INT64_C(100000000000)};
static std::atomic<int> clock_error{0};
static int controlled_clock(unsigned id, artbox_timespec *value) {
    CHECK(id == 1); if (clock_error.load()) return clock_error.load();
    int64_t now = controlled_time.load(); value->seconds = now / 1000000000; value->nanoseconds = now % 1000000000; return 0;
}
static void deterministic_deadlines(Context c, Probe &probe) {
    c.thread.system.clock = controlled_clock;
    const int64_t base = INT64_C(100000000000); controlled_time = base;
    int fd = create_timer(&c, 1, 0x80800); CHECK(fd >= 3);
    artbox_timer_spec spec{0, 2, 0, 3}, current{};
    CHECK(set_timer(&c, fd, 0, &spec, nullptr) == 0);
    uint64_t value[2]{};
    controlled_time = base + 2; CHECK(read_fd(&c, fd, value, 8) == -11);
    controlled_time = base + 3; CHECK(read_fd(&c, fd, value, 8) == 8 && value[0] == 1);
    controlled_time = base + 13;
    CHECK(get_timer(&c, fd, &current) == 0 && current.interval_nanoseconds == 2 && current.value_nanoseconds == 2);
    CHECK(read_fd(&c, fd, value, 8) == 8 && value[0] == 5);
    controlled_time = base + 14; CHECK(snapshot(&c, fd, 1) == 0);
    controlled_time = base + 15; CHECK(snapshot(&c, fd, 1) == 1);
    clock_error = -5; CHECK(read_fd(&c, fd, value, 8) == -5 && get_timer(&c, fd, &current) == -5);
    clock_error = 0; CHECK(read_fd(&c, fd, value, 8) == 8 && value[0] == 1);
    CHECK(close_fd(&c, fd) == 0);

    // Freeze the clock while closing/reusing the descriptor, then expire the
    // original timer retained only by its blocked read. This avoids relying
    // on a worker being scheduled within an arbitrary real-time interval.
    fd = create_timer(&c, 1, 0); int ep = static_cast<int>(call(c, 20)); CHECK(fd >= 3 && ep >= 3);
    CHECK(ctl(c, ep, 1, fd, 1, 111) == 0);
    Context worker = c; worker.scratch += 256; ++worker.thread.tid;
    std::atomic<unsigned> done{0}; int64_t result = -99; uint64_t old_value[2]{};
    std::thread waiter([&] { result = read_fd(&worker, fd, old_value, 8); done = 1; });
    await(probe.waiting, 1); spec = artbox_timer_spec{0, 0, 10, 0};
    CHECK(set_timer(&c, fd, 0, &spec, nullptr) == 0 && close_fd(&c, fd) == 0);
    int replacement = create_timer(&c, 1, 0x80800); CHECK(replacement == fd);
    CHECK(ctl(c, ep, 3, replacement, 1, 222) == -2 && ctl(c, ep, 1, replacement, 1, 222) == 0);
    spec = artbox_timer_spec{0, 0, 0, 1}; CHECK(set_timer(&c, replacement, 1, &spec, nullptr) == 0);
    controlled_time = base + INT64_C(11000000000);
    await(done, 1); waiter.join(); CHECK(result == 8 && old_value[0] == 1 && probe.live == 0);
    uint64_t cookies[2]{}; CHECK(poll(c, ep, cookies, 2) == 1 && cookies[0] == 222);
    CHECK(read_fd(&c, replacement, value, 8) == 8 && value[0] == 1);
    CHECK(close_fd(&c, replacement) == 0 && close_fd(&c, ep) == 0);
}
static void owner_and_admission(Context c, Probe &probe, const artbox_vm_ops &memory) {
    int fd = create_timer(&c, 1, 0); CHECK(fd >= 3);
    uint64_t value[2]{};
    probe.fail_create = true; CHECK(read_fd(&c, fd, value, 8) == -12 && probe.live == 0);
    probe.fail_wait = true; CHECK(read_fd(&c, fd, value, 8) == -5 && probe.live == 0);
    auto *foreign = artbox_vm_create(&memory, memory.page_size, 1); CHECK(foreign);
    Context other = c; other.thread.vm = foreign;
    CHECK(call(other, 87, fd, 0) == -95 && call(other, 63, fd, 0, 8) == -95);
    CHECK(artbox_vfs_events(c.fs, &other.thread, fd) == -95);
    CHECK(artbox_vm_destroy(foreign) == 0);
    CHECK(call(c, 80, fd, c.scratch) == -95 && call(c, 29, fd, 0, 0) == -25);
    CHECK(artbox_vfs_mmap(c.fs, c.thread.vm, 0, memory.page_size, 1, 1, fd, 0) == -19);
    CHECK(close_fd(&c, fd) == 0);
    std::vector<int> opened;
    for (unsigned i = 0; i < 8; ++i) { fd = create_timer(&c, 1, 0); CHECK(fd >= 3); opened.push_back(fd); }
    CHECK(create_timer(&c, 1, 0) == -24);
    for (int item : opened) CHECK(close_fd(&c, item) == 0);
}
int main(void) {
    auto memory = artbox_native_vm(); auto system = artbox_native_system();
    auto *vm = artbox_vm_create(&memory, memory.page_size * 4, 8); CHECK(vm);
    int64_t address = artbox_vm_mmap(vm, 0, memory.page_size * 2, 3, 0x22, -1, 0); CHECK(address > 0);
    Context c{}; c.fs = artbox_vfs_create(nullptr, 8); c.scratch = static_cast<uint64_t>(address);
    CHECK(c.fs && artbox_kernel_thread_init(&c.thread, vm, &system, 100, 100) == 0);
    CHECK(create_timer(&c, 1, 0) == -38);
    Probe probe; artbox_wake_ops wake{&probe, create_wake, signal_wake, wait_wake, close_wake};
    CHECK(artbox_vfs_set_epoll(c.fs, &wake, 8, 2) == 0);
    const artbox_timerfd_ops ops{create_timer, close_fd, set_timer, get_timer, read_fd, write_fd, snapshot, seek_fd, clock_ns, pause_test};
    CHECK(artbox_timerfd_check(&c, &ops) == 59);
    wake_checks(c, probe); closed_wait(c, probe);
    deterministic_deadlines(c, probe); owner_and_admission(c, probe, memory);
    int fd = create_timer(&c, 1, 0x80800), ep = static_cast<int>(call(c, 20)); CHECK(fd >= 3 && ep >= 3);
    artbox_timer_spec spec{0, 0, 0, 1}; CHECK(set_timer(&c, fd, 1, &spec, nullptr) == 0);
    CHECK(ctl(c, ep, 1, fd, 1, 111) == 0);
    uint64_t cookie = 0; CHECK(poll(c, ep, &cookie) == 1 && cookie == 111);
    CHECK(artbox_vm_mprotect(vm, c.scratch + memory.page_size, memory.page_size, 0) == 0);
    CHECK(call(c, 63, fd, c.scratch + memory.page_size - 4, 8) == 4);
    CHECK(poll(c, ep, &cookie) == 0);
    CHECK(close_fd(&c, fd) == 0 && poll(c, ep, &cookie) == 0);
    int replacement = create_timer(&c, 1, 0x80800); CHECK(replacement == fd);
    CHECK(ctl(c, ep, 3, replacement, 1, 222) == -2);
    CHECK(ctl(c, ep, 1, replacement, 1, 222) == 0 && set_timer(&c, replacement, 1, &spec, nullptr) == 0);
    CHECK(poll(c, ep, &cookie) == 1 && cookie == 222);
    CHECK(close_fd(&c, replacement) == 0 && close_fd(&c, ep) == 0);
    CHECK(create_timer(&c, 0, 0) == -95 && create_timer(&c, 7, 0) == -95);
    CHECK(probe.live == 0 && artbox_vfs_destroy(c.fs) == 0 && artbox_vm_destroy(vm) == 0);
    std::puts("{\"shared_cases\":59,\"wake_cases\":2,\"close_interrupt_cases\":1,\"partial_copy_bytes\":4,\"controlled_deadlines\":true,\"closed_timer_wakeup\":true,\"owner_admission\":true,\"passed\":true}");
    return 0;
}
