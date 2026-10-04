/* Original native wait/wake contract. SPDX-License-Identifier: MIT */
#include "artbox/native_wake.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#if defined(__APPLE__) || defined(__linux__)
#include <cerrno>
#include <csignal>
#include <pthread.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
using Clock = std::chrono::steady_clock;
static void await(const std::atomic<unsigned> &value, unsigned expected) {
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (value.load() != expected) {
        CHECK(Clock::now() < deadline);
        std::this_thread::yield();
    }
}
#if defined(__APPLE__) || defined(__linux__)
static void interrupt_wait(int signal) { (void)signal; }
static void interruption(const artbox_wake_ops &ops, void *owner) {
    struct sigaction action{}, previous{};
    action.sa_handler = interrupt_wait;
    CHECK(sigemptyset(&action.sa_mask) == 0);
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0);
    std::atomic<unsigned> entered{0}, done{0};
    int result = 0;
    std::thread worker([&] { entered = 1; result = ops.wait(owner, -1); done = 1; });
    await(entered, 1);
    /* Repeated delivery covers the interval between announcing entry and
     * entering the native syscall. EINTR proves that a wait was interrupted. */
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!done.load()) {
        CHECK(Clock::now() < deadline);
        int error = pthread_kill(worker.native_handle(), SIGUSR1);
        CHECK(error == 0 || error == ESRCH);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.join();
    CHECK(result == -4);
    CHECK(sigaction(SIGUSR1, &previous, nullptr) == 0);
    CHECK(ops.wait(owner, 0) == 0);
}
#endif
int main() {
    const artbox_wake_ops ops = artbox_native_wake();
    CHECK(ops.create && ops.signal && ops.wait && ops.close);
    void *first = nullptr, *second = nullptr;
    CHECK(ops.create(ops.context, &first) == 0 && first);
    CHECK(ops.create(ops.context, &second) == 0 && second && first != second);
    CHECK(ops.create(ops.context, nullptr) == -22);
    CHECK(ops.signal(nullptr) == -22 && ops.wait(nullptr, 0) == -22 && ops.close(nullptr) == -22);
    CHECK(ops.wait(first, -2) == -22 && ops.wait(first, 0) == 0);
    CHECK(ops.signal(second) == 0 && ops.wait(first, 0) == 0);
    CHECK(ops.wait(second, 0) == 1 && ops.wait(second, 0) == 0);
    for (unsigned i = 0; i < 100; ++i) CHECK(ops.signal(first) == 0);
    CHECK(ops.wait(first, 0) == 1 && ops.wait(first, 0) == 0);
    const auto before = Clock::now();
    CHECK(ops.wait(first, 30) == 0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - before).count();
    // Allow timer granularity and scheduling delay, but reject an immediate return.
    CHECK(elapsed >= 15 && elapsed < 3000);

    std::atomic<unsigned> entered{0}, consumed{0}, sequence{0};
    std::thread waiter([&] {
        entered = 1;
        for (unsigned i = 1; i <= 256; ++i) {
            CHECK(ops.wait(first, -1) == 1);
            CHECK(sequence.load() == i);
            consumed = i;
        }
    });
    await(entered, 1);
    for (unsigned i = 1; i <= 256; ++i) {
        sequence = i;
        CHECK(ops.signal(first) == 0);
        await(consumed, i);
    }
    waiter.join();
    CHECK(ops.wait(first, 0) == 0);

    // Concurrent producers may coalesce; they must leave one persistent hint.
    std::vector<std::thread> producers;
    for (unsigned i = 0; i < 8; ++i) producers.emplace_back([&] {
        for (unsigned j = 0; j < 2000; ++j) CHECK(ops.signal(first) == 0);
    });
    for (auto &producer : producers) producer.join();
    CHECK(ops.wait(first, 0) == 1 && ops.wait(first, 0) == 0);
    CHECK(ops.wait(second, 0) == 0);
#if defined(__APPLE__) || defined(__linux__)
    interruption(ops, first);
    const char *interrupted = "true";
#else
    const char *interrupted = "false";
#endif
    CHECK(ops.close(first) == 0 && ops.close(second) == 0);
    for (unsigned i = 0; i < 128; ++i) {
        void *owner = nullptr;
        CHECK(ops.create(ops.context, &owner) == 0 && owner);
        CHECK(ops.wait(owner, 0) == 0 && ops.signal(owner) == 0);
        CHECK(ops.wait(owner, 0) == 1 && ops.close(owner) == 0);
    }
#if defined(__APPLE__)
    const char *backend = "kqueue";
#elif defined(__linux__)
    const char *backend = "eventfd";
#else
    const char *backend = "windows-event";
#endif
    std::printf("{\"backend\":\"%s\",\"cross_thread_cycles\":256,\"concurrent_signals\":16000,"
                "\"timeout_ms\":%lld,\"native_interruption_verified\":%s,\"passed\":true}\n",
                backend, static_cast<long long>(elapsed), interrupted);
}
