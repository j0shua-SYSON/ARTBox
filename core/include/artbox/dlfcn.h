#ifndef ARTBOX_DLFCN_H
#define ARTBOX_DLFCN_H
#include "artbox/linker.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Android LP64 flags and special handles. Every image is a pinned dependency
 * of the same startup group; opening it acquires a handle, not new code pages. */
enum { ARTBOX_RTLD_LAZY=1, ARTBOX_RTLD_NOW=2, ARTBOX_RTLD_NOLOAD=4,
       ARTBOX_RTLD_GLOBAL=0x100, ARTBOX_RTLD_NODELETE=0x1000 };
#define ARTBOX_RTLD_DEFAULT UINT64_C(0)
#define ARTBOX_RTLD_NEXT UINT64_MAX
typedef struct artbox_dlfcn artbox_dlfcn;
typedef struct artbox_dl_error {
    char message[256];
    unsigned pending;
} artbox_dl_error;
typedef struct artbox_dl_alias {
    const char *path; /* Exact absolute guest path; never a host search path. */
    const char *module;
} artbox_dl_alias;

/* The group outlives this context and every guest call. Create after relocation,
 * before constructors that might call libdl. Aliases are copied. Unreachable
 * manifest entries cannot be opened. No library unload occurs before teardown.
 * The owner prevents mutation/destruction of the group while calls are active. */
artbox_elf_result artbox_dlfcn_create(const artbox_load_group *group,
    const artbox_dl_alias *aliases, unsigned alias_count, artbox_dlfcn **out);
void artbox_dlfcn_destroy(artbox_dlfcn *loader);

/* Each guest thread supplies its own zero-initialized error state. A failure
 * replaces it; reading clears pending status. Successful calls preserve an
 * unread earlier error, following the pinned Bionic entry implementation.
 * Strings and arguments are validated native pointers, copied by the platform
 * bridge from guest memory. These core functions do not access guest pointers. */
const char *artbox_dlerror(artbox_dl_error *error);
uint64_t artbox_dlopen(artbox_dlfcn *loader, artbox_dl_error *error,
    const char *name, unsigned flags);
int artbox_dlclose(artbox_dlfcn *loader, artbox_dl_error *error, uint64_t handle);
uint64_t artbox_dlsym(artbox_dlfcn *loader, artbox_dl_error *error,
    uint64_t handle, const char *name, const char *version, uint64_t caller);

/* Borrowed views for the platform's Android dladdr/dl_iterate_phdr ABI bridge.
 * Image names use the first configured alias, or their manifest name. Metadata
 * is immutable; iteration may reenter open/close/lookup without holding a lock. */
unsigned artbox_dlfcn_count(const artbox_dlfcn *loader);
artbox_elf_result artbox_dlfcn_info(const artbox_dlfcn *loader,
    unsigned index, artbox_link_info *out);
artbox_elf_result artbox_dlfcn_address(const artbox_dlfcn *loader,
    uint64_t address, artbox_link_address *out);

#ifdef __cplusplus
}
#endif
#endif
