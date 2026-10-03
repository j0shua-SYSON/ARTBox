/* Original ARTBox reference-contract bridge. This is not a complete ART ABI. */
#ifndef ARTBOX_ART_REFERENCE_BRIDGE_H
#define ARTBOX_ART_REFERENCE_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t artbox_art_reference_compress(const void *address);
void *artbox_art_reference_decompress(uint32_t reference);
#ifdef __cplusplus
}
#endif
#endif
