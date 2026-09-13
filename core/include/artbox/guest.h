#ifndef ARTBOX_GUEST_H
#define ARTBOX_GUEST_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_guest artbox_guest;
typedef struct artbox_memory_ops {
    size_t page_size;
    int (*map)(size_t size, unsigned protection, void **address);
    int (*protect)(void *address, size_t size, unsigned protection);
    int (*unmap)(void *address, size_t size);
} artbox_memory_ops;
/* Return a byte count or negative Linux errno. The output sink copies bytes
 * before returning if their lifetime needs to extend beyond the call. */
typedef int64_t (*artbox_output_fn)(void *context, int fd, const void *bytes, size_t count);
typedef int64_t (*artbox_dispatch_fn)(artbox_guest *, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
typedef void (*artbox_enter_fn)(const void *, artbox_guest *, artbox_dispatch_fn, void *);
#define ARTBOX_GUEST_RETURNED (-1)
#define ARTBOX_GUEST_INVALID (-2)

artbox_guest *artbox_guest_create(const artbox_memory_ops *memory, artbox_output_fn output,
                                void *context, const void *image, size_t image_size);
int artbox_guest_destroy(artbox_guest *guest);
int64_t artbox_guest_call(artbox_guest *guest, uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
/* Executes only through an explicitly supplied, precompiled platform entry.
 * The native arm64 bridge requires a signed, build-time-audited image. */
int artbox_guest_execute(artbox_guest *guest, const void *entry, artbox_enter_fn enter);
uint64_t artbox_guest_syscall_count(const artbox_guest *guest, uint64_t number);
#ifdef __cplusplus
}
#endif
#endif
