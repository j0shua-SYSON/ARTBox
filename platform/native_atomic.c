#include "artbox/native_atomic.h"

static uint32_t load_acquire(const void *address) {
#if defined(_MSC_VER) && !defined(__clang__)
    // This source is compiled with /volatile:ms. Naturally aligned 32-bit
    // Windows loads are atomic; MS volatile supplies acquire ordering. Unlike
    // an interlocked read-modify-write, this also works on read-only pages.
    return *(const volatile uint32_t *)address;
#else
    return __atomic_load_n((const uint32_t *)address, __ATOMIC_ACQUIRE);
#endif
}
static void store_release(void *address, uint32_t value) {
#if defined(_MSC_VER) && !defined(__clang__)
    *(volatile uint32_t *)address = value;
#else
    __atomic_store_n((uint32_t *)address, value, __ATOMIC_RELEASE);
#endif
}
artbox_atomic_u32_ops artbox_native_atomic_u32(void) {
    const artbox_atomic_u32_ops ops = {load_acquire, store_release};
    return ops;
}
