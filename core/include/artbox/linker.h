#ifndef ARTBOX_LINKER_H
#define ARTBOX_LINKER_H
#include "artbox/relocation.h"
#ifdef __cplusplus
extern "C" {
#endif

/* A bundle manifest entry. The platform must verify signed executable bytes,
 * load bias and writable storage before registering it. All borrowed metadata
 * and storage remain alive until the group is destroyed. No host OS library
 * search occurs. Entries not reachable through the root's DT_NEEDED are inert. */
typedef struct artbox_link_module {
    const char *name;
    const artbox_dynamic *dynamic;
    uint64_t load_bias;
    const artbox_relocation_memory *memory;
    unsigned memory_count;
} artbox_link_module;
typedef struct artbox_load_group artbox_load_group;
/* Fixed-width native/Android bridge. IDs start at one in dependency scope order.
 * Initialization bytes include data relocations after the group is relocated. */
typedef struct artbox_tls_template {
    uint64_t module_id, init_data, init_size, memory_size, alignment, skew;
} artbox_tls_template;
unsigned artbox_load_group_tls_count(const artbox_load_group *group);
artbox_elf_result artbox_load_group_tls_template(const artbox_load_group *group,
    unsigned index, artbox_tls_template *out);
/* Register a signed guest RX entry before relocation. Its platform contract is
 * a TLSDESC resolver returning an absolute address to build-adapted callers.
 * No guest thread may outlive this group's descriptor arguments/templates. */
artbox_elf_result artbox_load_group_tls_resolver(artbox_load_group *group, uint64_t address);
artbox_elf_result artbox_load_group_create(const artbox_link_module *modules, unsigned count,
    const char *root, artbox_relocation_resolver host_exports, void *host_context,
    artbox_load_group **out);
void artbox_load_group_destroy(artbox_load_group *group);
/* Relocate the whole reachable group transactionally. Missing strong imports
 * leave every module unchanged. Repeating a successful call is idempotent. */
artbox_elf_result artbox_load_group_relocate(artbox_load_group *group);
/* One Android-style breadth-first lookup scope, with DT_SYMBOLIC self priority
 * for relocation requests. Only explicit host_exports may supply native bridges.
 * Group mutation is serialized by the owner; callbacks must not mutate it. */
artbox_elf_result artbox_load_group_lookup(const artbox_load_group *group,
    const char *name, const char *version, uint64_t *address);
/* Dependencies initialize before their parents, with cycles visited once. All
 * constructor pointers are checked before the first callback. A failed callback
 * poisons initialization; it is never silently retried. No guest code runs here
 * except through the supplied precompiled platform bridge. */
artbox_elf_result artbox_load_group_initialize(artbox_load_group *group,
    artbox_elf_result (*invoke)(void *context, uint64_t address), void *context);
unsigned artbox_load_group_count(const artbox_load_group *group);
artbox_elf_result artbox_load_group_stats(const artbox_load_group *group,
    const char *name, artbox_relocation_stats *out);
#ifdef __cplusplus
}
#endif
#endif
