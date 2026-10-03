#include "artbox/managed_reference.h"

static int valid(const artbox_reference_window *window) {
    return window && window->base && !((window->base | window->length | window->guard_bytes) & 7) &&
           window->length <= UINT64_C(0x100000000) && window->guard_bytes >= 8 &&
           window->guard_bytes < window->length && window->length <= UINT64_MAX - window->base;
}

int artbox_reference_window_init(artbox_reference_window *window, uint64_t base,
                                 uint64_t length, uint64_t guard_bytes) {
    artbox_reference_window candidate = {base, length, guard_bytes};
    if (!window || !valid(&candidate)) return -22;
    *window = candidate;
    return 0;
}

int artbox_reference_encode(const artbox_reference_window *window, uint64_t address,
                            uint32_t *reference) {
    if (!reference || !valid(window)) return -22;
    if (address == 0) { *reference = 0; return 0; }
    if (address < window->base || (address & 7)) return -14;
    uint64_t offset = address - window->base;
    if (offset < window->guard_bytes || offset >= window->length) return -14;
    *reference = (uint32_t)offset;
    return 0;
}

int artbox_reference_decode(const artbox_reference_window *window, uint32_t reference,
                            uint64_t *address) {
    if (!address || !valid(window)) return -22;
    if (reference == 0) { *address = 0; return 0; }
    if ((reference & 7) || reference < window->guard_bytes || reference >= window->length) return -14;
    *address = window->base + reference;
    return 0;
}
