// Original bounded receive-buffer ownership contract. SPDX-License-Identifier: MIT
#include "artbox/binder_arena.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
static const uint64_t base = UINT64_C(0x401000000);

static void lifetime() {
    unsigned char bytes[256];
    std::memset(bytes, 0xa5, sizeof(bytes));
    CHECK(!artbox_binder_arena_create(nullptr, base, 256, 4));
    CHECK(!artbox_binder_arena_create(bytes, base + 1, 256, 4));
    CHECK(!artbox_binder_arena_create(bytes, UINT64_MAX - 7, 256, 4));
    CHECK(!artbox_binder_arena_create(bytes, base, 255, 4));
    CHECK(!artbox_binder_arena_create(bytes, base, 256, 0));
    artbox_binder_arena *arena = artbox_binder_arena_create(bytes, base, 256, 4);
    CHECK(arena);
    artbox_binder_buffer a, b, invalid;
    std::memset(&invalid, 0xab, sizeof(invalid)); a = invalid;
    CHECK(artbox_binder_arena_reserve(arena, UINT64_MAX, 0, 0, 0, &a) == -12);
    CHECK(!std::memcmp(&a, &invalid, sizeof(a)));
    CHECK(artbox_binder_arena_reserve(arena, 8, 1, 0, 0, &a) == -22);
    CHECK(artbox_binder_arena_reserve(arena, 0, 0, 0, 2, &a) == -22);
    CHECK(artbox_binder_arena_reserve(arena, 13, 8, 1, 1, &a) == 0);
    CHECK(a.address == base && a.extent == 32);
    CHECK(a.data_size == 13 && a.offsets_address == base + 16 && a.offsets_size == 8);
    CHECK(a.extra_address == base + 24 && a.extra_size == 1);
    for (size_t i = 0; i < 32; ++i) CHECK(bytes[i] == 0);
    CHECK(bytes[32] == 0xa5);
    CHECK(artbox_binder_arena_destroy(arena) == -16);
    const char payload[] = "hello binder";
    CHECK(artbox_binder_arena_write(arena, a.address, 0, payload, sizeof(payload)) == 0);
    CHECK(!std::memcmp(bytes, payload, sizeof(payload)));
    CHECK(artbox_binder_arena_write(arena, a.address, 32, nullptr, 0) == 0);
    CHECK(artbox_binder_arena_write(arena, a.address, 32, payload, 1) == -22);
    CHECK(artbox_binder_arena_write(arena, a.address, UINT64_MAX, payload, 1) == -22);
    CHECK(artbox_binder_arena_write(arena, a.address, 0, nullptr, 1) == -22);
    CHECK(artbox_binder_arena_release(arena, a.address) == -1); // Not delivered.
    CHECK(artbox_binder_arena_publish(arena, a.address + 8) == -22);
    CHECK(artbox_binder_arena_publish(arena, a.address) == 0);
    CHECK(artbox_binder_arena_publish(arena, a.address) == -1);
    CHECK(artbox_binder_arena_write(arena, a.address, 0, payload, 1) == -1);
    CHECK(artbox_binder_arena_cancel(arena, a.address) == -1);
    CHECK(artbox_binder_arena_release(arena, a.address + 8) == -22);
    CHECK(artbox_binder_arena_release(arena, a.address) == 0);
    CHECK(artbox_binder_arena_release(arena, a.address) == -22);
    for (size_t i = 0; i < 32; ++i) CHECK(bytes[i] == 0); // Clear-on-free.
    CHECK(artbox_binder_arena_reserve(arena, 0, 0, 0, 0, &a) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 0, 0, 0, 0, &b) == 0);
    CHECK(a.extent == 8 && b.extent == 8 && a.address != b.address);
    CHECK(artbox_binder_arena_cancel(arena, a.address) == 0);
    CHECK(artbox_binder_arena_cancel(arena, b.address) == 0);
    CHECK(artbox_binder_arena_destroy(arena) == 0);
}

static void exhaustion() {
    unsigned char bytes[256];
    artbox_binder_arena *arena = artbox_binder_arena_create(bytes, base, sizeof(bytes), 4);
    CHECK(arena);
    artbox_binder_buffer a, b, c, d, e;
    CHECK(artbox_binder_arena_reserve(arena, 32, 0, 0, 0, &a) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 64, 0, 0, 0, &b) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 32, 0, 0, 0, &c) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 128, 0, 0, 0, &d) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 0, 0, 0, 0, &e) == -12);
    CHECK(artbox_binder_arena_cancel(arena, b.address) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 65, 0, 0, 0, &e) == -12);
    CHECK(artbox_binder_arena_reserve(arena, 49, 8, 0, 0, &e) == 0 && e.address == b.address);
    CHECK(artbox_binder_arena_cancel(arena, a.address) == 0);
    CHECK(artbox_binder_arena_cancel(arena, e.address) == 0);
    CHECK(artbox_binder_arena_cancel(arena, c.address) == 0);
    CHECK(artbox_binder_arena_cancel(arena, d.address) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 256, 0, 0, 0, &e) == 0 && e.address == base);
    CHECK(artbox_binder_arena_cancel(arena, e.address) == 0);
    // Exhaust metadata independently of bytes.
    artbox_binder_buffer small[4];
    for (auto &value : small) CHECK(artbox_binder_arena_reserve(arena, 0, 0, 0, 0, &value) == 0);
    CHECK(artbox_binder_arena_reserve(arena, 0, 0, 0, 0, &e) == -12);
    for (auto value : small) CHECK(artbox_binder_arena_cancel(arena, value.address) == 0);
    // Reused storage never exposes previous payload or alignment padding.
    std::memset(bytes, 0xbc, sizeof(bytes));
    CHECK(artbox_binder_arena_reserve(arena, 1, 8, 1, 0, &e) == 0);
    for (size_t i = 0; i < e.extent; ++i) CHECK(bytes[i] == 0);
    CHECK(artbox_binder_arena_cancel(arena, e.address) == 0);
    CHECK(artbox_binder_arena_destroy(arena) == 0);
}

static void concurrency() {
    unsigned char bytes[4096];
    artbox_binder_arena *arena = artbox_binder_arena_create(bytes, base, sizeof(bytes), 16);
    CHECK(arena);
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (unsigned i = 1; i <= 8; ++i) workers.emplace_back([&, i] {
        unsigned char pattern[48]; std::memset(pattern, static_cast<int>(i), sizeof(pattern));
        ready.fetch_add(1);
        while (!start.load()) std::this_thread::yield();
        for (unsigned n = 0; n < 1000; ++n) {
            artbox_binder_buffer value;
            CHECK(artbox_binder_arena_reserve(arena, sizeof(pattern), 8, 0, 1, &value) == 0);
            CHECK(artbox_binder_arena_write(arena, value.address, 0, pattern, sizeof(pattern)) == 0);
            CHECK(artbox_binder_arena_publish(arena, value.address) == 0);
            CHECK(!std::memcmp(bytes + (value.address - base), pattern, sizeof(pattern)));
            CHECK(artbox_binder_arena_release(arena, value.address) == 0);
        }
    });
    while (ready.load() != 8) std::this_thread::yield();
    start.store(true);
    for (auto &worker : workers) worker.join();
    CHECK(artbox_binder_arena_destroy(arena) == 0);
}

int main() {
    lifetime(); exhaustion(); concurrency();
    std::puts("Binder arena: reserve/publish/release, bounds, exhaustion, recycling and 8000 concurrent lifecycles pass");
    return 0;
}
