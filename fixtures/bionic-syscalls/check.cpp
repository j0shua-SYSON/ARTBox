// Native Linux oracle for actual NDK-assembled Bionic entries. All bulk ABI
// cases use a capture backend; only the five explicit smoke cases reach Linux.
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>
#include "syscall-test.h"

struct Invocation { int64_t value; uint64_t damaged_registers; };
extern "C" Invocation artbox_invoke_stub(void (*)(), const uint64_t*);
static std::atomic<unsigned> failures{0}, cases{0};
struct State {
    bool capture;
    uint64_t number, arguments[6];
    int64_t result;
    unsigned calls;
    int guest_errno;
};
static thread_local State state{};

// The linked definition of __set_errno_internal is the pinned Bionic source.
// Its __errno dependency uses separate test TLS, never glibc's errno storage.
extern "C" int* artbox_stub___errno() { return &state.guest_errno; }
extern "C" int64_t artbox_stub_artbox_bionic_syscall(uint64_t number, uint64_t a0, uint64_t a1,
                                                    uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (state.capture) {
        ++state.calls;
        const uint64_t actual[] = {a0, a1, a2, a3, a4, a5};
        if (number != state.number) ++failures;
        for (unsigned i = 0; i < 6; ++i) if (actual[i] != state.arguments[i]) ++failures;
        return state.result;
    }
    const int saved = errno;
    long result = syscall(number, a0, a1, a2, a3, a4, a5);
    const int error = errno;
    errno = saved;
    return result == -1 ? -error : result;
}

static bool raw_error(int64_t value) { return value < 0 && value >= -4095; }
static void capture_case(void (*function)(), uint64_t number, unsigned count, bool generic, int64_t result) {
    uint64_t registers[7]{};
    state = {};
    state.capture = true;
    state.number = number;
    state.result = result;
    state.guest_errno = 73;
    if (generic) registers[0] = number;
    for (unsigned i = 0; i < 6; ++i) {
        uint64_t value = UINT64_C(0x8192837465a0b000) + i * 0x101;
        registers[i + (generic ? 1 : 0)] = value;
        state.arguments[i] = i < count ? value : 0;
    }
    errno = EDOM;
    Invocation actual = artbox_invoke_stub(function, registers);
    if (state.calls != 1 || actual.damaged_registers || errno != EDOM ||
        actual.value != (raw_error(result) ? -1 : result) ||
        state.guest_errno != (raw_error(result) ? -result : 73)) ++failures;
    ++cases;
}

static void capture_worker() {
    const int64_t values[] = {0, 1, INT64_MAX, -1, -2, -4095, -4096, INT64_MIN, INT64_C(0x100000002)};
    for (const auto& stub : stubs) for (int64_t value : values)
        capture_case(stub.function, stub.number, stub.arguments, false, value);
    for (int64_t value : values)
        capture_case(artbox_stub_syscall, UINT64_C(0x8192000012345678), 6, true, value);
}

static void smoke(void (*function)(), const uint64_t* arguments, int64_t result, int error) {
    state = {};
    state.guest_errno = 73;
    errno = EDOM;
    Invocation actual = artbox_invoke_stub(function, arguments);
    if (actual.value != result || actual.damaged_registers || state.guest_errno != error || errno != EDOM) ++failures;
}

int main() {
#ifdef ARTBOX_ADAPTED_STUBS
    std::thread workers[] = {std::thread(capture_worker), std::thread(capture_worker),
                             std::thread(capture_worker), std::thread(capture_worker)};
    for (auto& worker : workers) worker.join();
#else
    (void)capture_worker;
#endif
    uint64_t zero[7]{};
    uint64_t bad_write[7] = {UINT64_MAX, 0, 0, 0, 0, 0, 0};
    uint64_t bad_map[7] = {0, 0, 1, 0x22, UINT64_MAX, 0, 0};
    uint64_t unknown[7] = {UINT64_MAX, 0, 0, 0, 0, 0, 0};
    smoke(artbox_stub___getpid, zero, getpid(), 73);
    smoke(artbox_stub_getuid, zero, getuid(), 73);
    smoke(artbox_stub_write, bad_write, -1, 9);
    smoke(artbox_stub_mmap, bad_map, -1, 22);
    smoke(artbox_stub_syscall, unknown, -1, 38);
    std::printf("{\"functions\":%zu,\"capture_cases\":%u,\"smoke_cases\":5,\"failures\":%u}\n",
                sizeof(stubs) / sizeof(stubs[0]) + 1, cases.load(), failures.load());
    return failures ? 1 : 0;
}
