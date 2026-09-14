#include "artbox/native_syscall.h"
#include "artbox/native_memory.h"
#include "artbox/native_tls.h"
#include "artbox/guest.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static std::atomic<unsigned> failures{0};
struct Capture { uint64_t seed; int64_t result; unsigned calls; void** tls; };
static int64_t capture(void* opaque, uint64_t number, uint64_t a0, uint64_t a1,
                       uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    auto* state = static_cast<Capture*>(opaque);
    const uint64_t values[] = {a0, a1, a2, a3, a4, a5};
    if (number != UINT64_C(0x123456789abc) || artbox_bionic_get_tls() != state->tls) ++failures;
    for (unsigned i = 0; i < 6; ++i) if (values[i] != state->seed + i) ++failures;
    ++state->calls;
    errno = EINTR;
    return state->result;
}
static void check_capture(Capture& state) {
    errno = EDOM;
    uint64_t s = state.seed;
    int64_t value = artbox_bionic_syscall(UINT64_C(0x123456789abc), s, s+1, s+2, s+3, s+4, s+5);
    if (value != state.result || errno != EDOM) ++failures;
}

static int64_t translate(void* context, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5) {
    errno = EIO;
    return artbox_guest_call(static_cast<artbox_guest*>(context), n, a0, a1, a2, a3, a4, a5);
}
static int64_t output(void* context, int fd, const void* bytes, size_t size) {
    if (fd != 1 || size != 5 || std::memcmp(bytes, "hello", 5)) ++failures;
    ++*static_cast<unsigned*>(context);
    return static_cast<int64_t>(size);
}
static void exit_entry(const void*, artbox_guest*, artbox_dispatch_fn, void*) {
    artbox_bionic_syscall(93, 0x123, 0, 0, 0, 0, 0);
    ++failures;
}

int main() {
    static constexpr unsigned workers = 12, iterations = 1000;
    if (artbox_native_syscall_swap(nullptr) != nullptr || artbox_bionic_syscall(64, 0, 0, 0, 0, 0, 0) != -38) return 1;
    const artbox_syscall_binding invalid{nullptr, nullptr};
    artbox_native_syscall_swap(&invalid);
    if (artbox_bionic_syscall(64, 0, 0, 0, 0, 0, 0) != -38) return 1;
    std::vector<std::thread> threads;
    for (unsigned id = 0; id < workers; ++id) threads.emplace_back([id] {
        if (artbox_native_syscall_swap(nullptr) != nullptr) ++failures;
        void* slots[2] = {nullptr, nullptr};
        artbox_native_tls_swap(slots);
        Capture outer{UINT64_C(0x8000000000000000) + id * 100, -4095, 0, slots};
        Capture inner{UINT64_C(0x100000000) + id * 100, INT64_MAX, 0, slots};
        const artbox_syscall_binding a{capture, &outer}, b{capture, &inner};
        if (artbox_native_syscall_swap(&a) != nullptr) ++failures;
        for (unsigned i = 0; i < iterations; ++i) {
            check_capture(outer);
            const auto* previous = artbox_native_syscall_swap(&b);
            std::this_thread::yield();
            if (previous != &a) ++failures;
            check_capture(inner);
            if (artbox_native_syscall_swap(previous) != &b) ++failures;
        }
        if (outer.calls != iterations || inner.calls != iterations || artbox_native_syscall_swap(nullptr) != &a) ++failures;
        artbox_native_tls_swap(nullptr);
    });
    for (auto& thread : threads) thread.join();
    if (artbox_native_syscall_swap(nullptr) != &invalid) ++failures;

    // Exercise the existing five-syscall translator through the exact exported
    // seven-word entry, including exit's non-local return and binding cleanup.
    const char message[] = "hello";
    unsigned writes = 0;
    artbox_memory_ops memory = artbox_native_memory();
    artbox_guest* guest = artbox_guest_create(&memory, output, &writes, message, sizeof(message));
    if (!guest) return 1;
    const artbox_syscall_binding binding{translate, guest};
    artbox_native_syscall_swap(&binding);
    errno = EDOM;
    if (artbox_bionic_syscall(64, 1, reinterpret_cast<uintptr_t>(message), 5, 0, 0, 0) != 5 || errno != EDOM) ++failures;
    if (artbox_bionic_syscall(64, 99, 0, 0, 0, 0, 0) != -9 || errno != EDOM) ++failures;
    int64_t mapped = artbox_bionic_syscall(222, 0, memory.page_size, 3, 0x22, UINT64_MAX, 0);
    if (mapped <= 0) return 1;
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(mapped)), message, sizeof(message));
    if (artbox_bionic_syscall(226, mapped, memory.page_size, 1, 0, 0, 0) != 0) ++failures;
    if (artbox_bionic_syscall(64, 1, mapped, 5, 0, 0, 0) != 5) ++failures;
    if (artbox_bionic_syscall(215, mapped, memory.page_size, 0, 0, 0, 0) != 0) ++failures;
    if (artbox_bionic_syscall(9999, 0, 0, 0, 0, 0, 0) != -38 || errno != EDOM) ++failures;
    if (artbox_guest_execute(guest, message, exit_entry) != 0x23) ++failures;
    if (artbox_native_syscall_swap(nullptr) != &binding || writes != 2) ++failures;
    if (artbox_guest_destroy(guest) != 0) ++failures;
    if (failures) { std::fprintf(stderr, "%u native syscall boundary failures\n", failures.load()); return 1; }
    std::printf("%u native thread bindings, %u nested dispatches each; five syscall translation and exit verified\n", workers, iterations);
}
