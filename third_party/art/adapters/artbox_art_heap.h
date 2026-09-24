/* Original ARTBox adapter. Bind before ART startup, unbind after all ART threads
 * and references are gone, then destroy the owning VM. One runtime per process.
 * Mapping arguments/errors use Linux values, as in the compiled AOSP payload. */
#ifndef ARTBOX_ART_HEAP_H
#define ARTBOX_ART_HEAP_H
#include "artbox/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
int artbox_art_heap_initialize(artbox_vm* vm, uint64_t length, size_t guard);
void artbox_art_heap_unbind(void);
artbox_reference_window artbox_art_heap_window(void);
int artbox_art_heap_overlaps(const void* address, size_t length);
void* artbox_art_heap_map(void* address, size_t length, int prot, int flags, int fd, int64_t offset);
int artbox_art_heap_unmap(void* address, size_t length);
int artbox_art_heap_protect(void* address, size_t length, int prot);
uint32_t artbox_art_reference_compress(const void* address);
void* artbox_art_reference_decompress(uint32_t reference);
#ifdef __cplusplus
}
#endif
#endif
