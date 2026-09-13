#include "gwp_asan/platform_specific/guarded_pool_allocator_tls.h"
#include <new>

// Compile this caller with the same pinned headers and flags as the allocator.
// The Linux driver supplies aligned storage and checks the actual bit fields.
extern "C" unsigned artbox_allocator_tls_size() {
    return sizeof(gwp_asan::ThreadLocalPackedVariables);
}
extern "C" void artbox_allocator_tls_init(void* storage) {
    new (storage) gwp_asan::ThreadLocalPackedVariables;
}
extern "C" unsigned artbox_allocator_tls_exchange(unsigned random, unsigned counter, unsigned recursive,
                                                 unsigned* old_random, unsigned* old_counter, unsigned* old_recursive) {
    auto* state = gwp_asan::getThreadLocals();
    *old_random = state->RandomState;
    *old_counter = state->NextSampleCounter;
    *old_recursive = state->RecursiveGuard;
    state->RandomState = random;
    state->NextSampleCounter = counter;
    state->RecursiveGuard = recursive != 0;
    return state->NextSampleCounter;
}
