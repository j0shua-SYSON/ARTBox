#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

extern "C" unsigned artbox_allocator_tls_size();
extern "C" void artbox_allocator_tls_init(void*);
extern "C" unsigned artbox_allocator_tls_exchange(unsigned, unsigned, unsigned, unsigned*, unsigned*, unsigned*);

static thread_local void* slots[10];
extern "C" void** artbox_bionic_get_tls() { return slots + 2; }
static std::atomic<unsigned> failures{0};

int main() {
    if (artbox_allocator_tls_size() != sizeof(uint64_t)) return 1;
    static constexpr unsigned workers = 8, iterations = 1024;
    std::vector<std::thread> threads;
    for (unsigned id = 0; id < workers; ++id) threads.emplace_back([id] {
        alignas(8) unsigned char state[8];
        artbox_allocator_tls_init(state);
        // AArch64 TLS_SLOT_NATIVE_BRIDGE_GUEST_STATE is -2. All other slots
        // remain null so an accidental slot selection cannot pass this test.
        slots[0] = state;
        unsigned expected_random = 0xacd979ce, expected_counter = 0, expected_recursive = 0;
        for (unsigned i = 0; i < iterations; ++i) {
            unsigned old_random, old_counter, old_recursive;
            unsigned random = 0x80000000U + id * iterations + i;
            unsigned counter = (i & 1) ? 0x7fffffffU : i;
            errno = EDOM;
            unsigned stored = artbox_allocator_tls_exchange(random, counter, i & 1,
                                                            &old_random, &old_counter, &old_recursive);
            if (old_random != expected_random || old_counter != expected_counter ||
                old_recursive != expected_recursive || stored != counter || errno != EDOM) ++failures;
            expected_random = random; expected_counter = counter; expected_recursive = i & 1;
            std::this_thread::yield();
        }
        slots[0] = nullptr;
    });
    for (auto& thread : threads) thread.join();
    if (failures) { std::fprintf(stderr, "%u allocator TLS failures\n", failures.load()); return 1; }
    std::printf("{\"threads\":%u,\"exchanges\":%u,\"failures\":0}\n", workers, workers * iterations);
}
