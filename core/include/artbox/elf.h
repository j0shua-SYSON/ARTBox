#ifndef ARTBOX_ELF_H
#define ARTBOX_ELF_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ARTBOX_ELF_MAX_SEGMENTS 16
typedef enum artbox_elf_result {
    ARTBOX_ELF_OK = 0,
    ARTBOX_ELF_INVALID,
    ARTBOX_ELF_UNSUPPORTED,
    ARTBOX_ELF_NOT_FOUND
} artbox_elf_result;

typedef struct artbox_elf_segment {
    uint64_t virtual_address, memory_size, file_offset, file_size, alignment;
    uint32_t flags;
} artbox_elf_segment;

/* A borrowed data view. Validation never allocates or makes code executable. */
typedef struct artbox_elf {
    const unsigned char *data;
    size_t size;
    uint64_t entry;
    unsigned segment_count;
    artbox_elf_segment segments[ARTBOX_ELF_MAX_SEGMENTS];
} artbox_elf;

typedef struct artbox_elf_section {
    uint64_t address, offset, size, flags;
    uint32_t type;
} artbox_elf_section;

artbox_elf_result artbox_elf_validate(const void *data, size_t size, artbox_elf *out);
artbox_elf_result artbox_elf_find_section(const artbox_elf *image, const char *name,
                                        artbox_elf_section *out);
const char *artbox_elf_result_string(artbox_elf_result result);
#ifdef __cplusplus
}
#endif
#endif
