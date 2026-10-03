#ifndef ARTBOX_MANAGED_REFERENCE_H
#define ARTBOX_MANAGED_REFERENCE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A checked byte-offset codec for one managed heap window, not a heap allocator.
 * References retain 8-byte object alignment and use zero exclusively for null.
 * The caller owns the memory and must keep the base stable while references live.
 * This API does not establish that a referenced address contains a live object. */
typedef struct artbox_reference_window {
    uint64_t base;
    uint64_t length;
    uint64_t guard_bytes;
} artbox_reference_window;

/* The window is at most 4 GiB and begins with an inaccessible guard of at least
 * eight bytes. Inputs and lengths are 8-byte aligned. Output remains unchanged
 * on error: -EINVAL for invalid configuration, -EFAULT for an invalid reference.
 * Outputs must not alias the window. No function dereferences a guest address. */
int artbox_reference_window_init(artbox_reference_window *window, uint64_t base,
                                 uint64_t length, uint64_t guard_bytes);
int artbox_reference_encode(const artbox_reference_window *window, uint64_t address,
                            uint32_t *reference);
int artbox_reference_decode(const artbox_reference_window *window, uint32_t reference,
                            uint64_t *address);

#ifdef __cplusplus
}
#endif
#endif
