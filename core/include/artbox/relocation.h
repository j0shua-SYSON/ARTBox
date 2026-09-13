#ifndef ARTBOX_RELOCATION_H
#define ARTBOX_RELOCATION_H
#include "artbox/dynamic.h"
#ifdef __cplusplus
extern "C" {
#endif

/* A complete, readable/writable PT_LOAD view, before applying RELRO. The
 * original ELF bytes must remain separate and immutable. Code has no view. */
typedef struct artbox_relocation_memory {
    unsigned segment_index;
    unsigned char *data;
    size_t size;
} artbox_relocation_memory;

typedef struct artbox_relocation_stats {
    size_t rela_count, plt_count, relr_count;
} artbox_relocation_stats;

/* Resolve a default-visible, nonlocal symbol in the caller's lookup scope.
 * Return its final runtime address, or NOT_FOUND. Definitions in this image
 * are offered for interposition too; absent a scope definition, they bind here.
 * This callback must not mutate the image, destination views or loader state,
 * invoke guest code, or return an unprocessed TLS/IFUNC value. */
typedef artbox_elf_result (*artbox_relocation_resolver)(void *context,
    const artbox_dynamic *dynamic, uint32_t symbol_index, uint64_t *address);

/* Apply AArch64 ABS64/GLOB_DAT/JUMP_SLOT/RELATIVE and RELR to data views.
 * load_bias is the ELF load-time address adjustment, not a data-view pointer.
 * These views can be staging buffers; packaging must prove the actual signed
 * runtime layout has this bias before executing anything. No code is invoked,
 * made writable or allocated executable here. The caller owns synchronization.
 *
 * All targets and bindings are prepared before any destination byte is written.
 * On error, destinations and stats are unchanged. Missing weak symbols use 0;
 * missing strong symbols fail. NONE is ignored. Overlapping/composed writes,
 * TLS, IFUNC, COPY and instruction relocations are explicitly unsupported.
 * Each call is for a fresh, not-yet-relocated image; RELR is not idempotent. */
artbox_elf_result artbox_relocate(const artbox_dynamic *dynamic, uint64_t load_bias,
    const artbox_relocation_memory *memory, unsigned memory_count,
    artbox_relocation_resolver resolve, void *context, artbox_relocation_stats *stats);

#ifdef __cplusplus
}
#endif
#endif
