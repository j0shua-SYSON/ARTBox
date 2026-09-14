#include "artbox/native_tls.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

static thread_local unsigned host_sentinel;

int main() {
    constexpr unsigned workers = 12, iterations = 10000;
    std::atomic<unsigned> arrived{0}, failures{0};
    std::atomic<bool> start{false};
    void *main_slots[2] = {&arrived, &failures};
    if (artbox_bionic_get_tls() != nullptr || artbox_native_tls_swap(main_slots) != nullptr) return 1;
    host_sentinel = 0x1234;
    std::vector<std::thread> threads;
    for (unsigned id = 0; id < workers; ++id) threads.emplace_back([&, id] {
        unsigned a = id + 1, b = id + 100;
        void *outer[2] = {&a, &b}, *inner[2] = {&b, &a};
        if (artbox_bionic_get_tls() != nullptr) ++failures;
        host_sentinel = id + 0x8000;
        ++arrived;
        while (!start.load()) std::this_thread::yield();
        if (artbox_native_tls_swap(outer) != nullptr) ++failures;
        for (unsigned i = 0; i < iterations; ++i) {
            if (artbox_bionic_get_tls() != outer || artbox_bionic_get_tls()[0] != &a) ++failures;
            void **previous = artbox_native_tls_swap(inner);
            std::this_thread::yield();
            if (previous != outer || artbox_bionic_get_tls() != inner ||
                artbox_bionic_get_tls()[0] != &b || host_sentinel != id + 0x8000) ++failures;
            /* The precompiled setter returns success without changing host TLS. */
            if (artbox_bionic_set_tls(previous) != 0 || artbox_bionic_get_tls() != outer) ++failures;
        }
        if (artbox_native_tls_swap(nullptr) != outer || artbox_bionic_get_tls() != nullptr) ++failures;
    });
    while (arrived.load() != workers) std::this_thread::yield();
    start = true;
    for (auto &thread : threads) thread.join();
    if (artbox_bionic_get_tls() != main_slots || host_sentinel != 0x1234) ++failures;
    if (artbox_native_tls_swap(nullptr) != main_slots || artbox_bionic_get_tls() != nullptr) ++failures;
    /* A new native thread starts with no guest binding even after other threads exit. */
    std::thread fresh([&] { if (artbox_bionic_get_tls() != nullptr) ++failures; });
    fresh.join();
    if (failures.load()) {
        std::fprintf(stderr, "%u guest TLS isolation failures\n", failures.load()); return 1;
    }
    std::printf("%u host threads, %u nested guest TLS switches each; host TLS preserved\n", workers, iterations);
    return 0;
}
