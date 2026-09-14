#ifndef ARTBOX_DYNAMIC_H
#define ARTBOX_DYNAMIC_H
#include "artbox/elf.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ARTBOX_ELF_MAX_NEEDED 64
#define ARTBOX_ELF_MAX_VERSIONS 256
typedef struct artbox_elf_version {
    const char *name, *file; /* file is the DT_NEEDED provider; NULL for definitions. */
    uint32_t hash;
    uint16_t index, flags;
    unsigned hidden;
} artbox_elf_version;
typedef struct artbox_elf_table {
    const unsigned char *data;
    uint64_t address, size;
} artbox_elf_table;

/* Borrowed, validated metadata. Keep both the image and its bytes immutable.
 * This view neither applies relocations nor invokes guest functions. */
typedef struct artbox_dynamic {
    const artbox_elf *image;
    const char *strings, *soname, *rpath, *runpath;
    size_t strings_size;
    const char *needed[ARTBOX_ELF_MAX_NEEDED];
    unsigned needed_count;
    const unsigned char *symbols;
    uint32_t symbol_count;
    artbox_elf_table rela, plt_rela, relr, init_array, fini_array, preinit_array;
    uint64_t init, fini, flags, flags_1, plt_got;
    const unsigned char *sysv_buckets, *sysv_chains;
    uint32_t sysv_bucket_count;
    const unsigned char *gnu_bloom, *gnu_buckets, *gnu_chains;
    uint32_t gnu_bucket_count, gnu_symbol_offset, gnu_bloom_count, gnu_shift;
    const unsigned char *versym;
    unsigned version_count;
    artbox_elf_version versions[ARTBOX_ELF_MAX_VERSIONS];
} artbox_dynamic;

typedef struct artbox_elf_symbol {
    const char *name;
    uint64_t value, size;
    uint16_t section;
    unsigned char binding, type, visibility;
} artbox_elf_symbol;

/* REL/Android packed relocations, text relocations and variant PCS are
 * explicit unsupported results in this initial dynamic-table implementation. */
artbox_elf_result artbox_dynamic_open(const artbox_elf *image, artbox_dynamic *out);
artbox_elf_result artbox_dynamic_symbol(const artbox_dynamic *dynamic, uint32_t index,
                                      artbox_elf_symbol *out);
/* Find a defined, externally visible symbol within this image. The result is
 * an ELF value/type, never a directly callable host function pointer. */
artbox_elf_result artbox_dynamic_lookup(const artbox_dynamic *dynamic, const char *name,
                                      artbox_elf_symbol *out);
/* Decode a symbol's validated version requirement or definition. An unversioned
 * symbol has name=NULL. Numeric indices belong only to their originating image. */
artbox_elf_result artbox_dynamic_version(const artbox_dynamic *dynamic, uint32_t index,
                                       artbox_elf_version *out);
/* A NULL version selects a default-visible version. Named lookup permits hidden
 * versions. Following AOSP, an unversioned DSO/global symbol may interpose; the
 * load-group owner separately validates each DT_NEEDED provider's requirements. */
artbox_elf_result artbox_dynamic_lookup_version(const artbox_dynamic *dynamic, const char *name,
                                              const char *version, artbox_elf_symbol *out);
#ifdef __cplusplus
}
#endif
#endif
