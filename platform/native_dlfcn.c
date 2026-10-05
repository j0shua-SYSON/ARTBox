// SPDX-License-Identifier: MIT
#include "artbox/native_dlfcn.h"
#include <string.h>

#if defined(_MSC_VER)
static __declspec(thread) artbox_guest_dl_thread *current;
#else
static _Thread_local artbox_guest_dl_thread *current;
#endif
artbox_guest_dl_thread *artbox_native_dlfcn_swap(artbox_guest_dl_thread *thread) {
    artbox_guest_dl_thread *previous = current;
    current = thread;
    return previous;
}
static void *loader_open(const char *name, int flags, const void *caller) {
    (void)caller;
    return (void *)(uintptr_t)artbox_guest_dlopen(current, (uintptr_t)name, (unsigned)flags);
}
static void *loader_open_ext(const char *name, int flags, const void *extinfo, const void *caller) {
    (void)caller;
    return (void *)(uintptr_t)artbox_guest_android_dlopen_ext(current,
        (uintptr_t)name, (unsigned)flags, (uintptr_t)extinfo);
}
static void *loader_namespace(const char *name) {
    return (void *)(uintptr_t)artbox_guest_android_get_exported_namespace(current, (uintptr_t)name);
}
static char *loader_error(void) {
    return (char *)(uintptr_t)artbox_guest_dlerror(current);
}
static void *loader_symbol(void *handle, const char *name, const void *caller) {
    return (void *)(uintptr_t)artbox_guest_dlsym(current, (uintptr_t)handle,
        (uintptr_t)name, 0, (uintptr_t)caller);
}
static void *loader_version(void *handle, const char *name, const char *version, const void *caller) {
    return (void *)(uintptr_t)artbox_guest_dlsym(current, (uintptr_t)handle,
        (uintptr_t)name, (uintptr_t)version, (uintptr_t)caller);
}
static int loader_address(const void *address, void *output) {
    return artbox_guest_dladdr(current, (uintptr_t)address, (uintptr_t)output);
}
static int loader_close(void *handle) {
    return artbox_guest_dlclose(current, (uintptr_t)handle);
}
static int loader_iterate(uint64_t callback, void *data) {
    return artbox_guest_dl_iterate_phdr(current, callback, (uintptr_t)data);
}
artbox_elf_result artbox_native_dlfcn_resolve(void *context,
    const artbox_dynamic *dynamic, uint32_t index, uint64_t *address) {
    (void)context;
    if (!dynamic || !address) return ARTBOX_ELF_INVALID;
    if (!dynamic->soname || (strcmp(dynamic->soname, "libdl.so") &&
        strcmp(dynamic->soname, "libdl_android.so"))) return ARTBOX_ELF_NOT_FOUND;
    artbox_elf_symbol symbol;
    artbox_elf_version version;
    artbox_elf_result r = artbox_dynamic_symbol(dynamic, index, &symbol);
    if (r != ARTBOX_ELF_OK) return r;
    r = artbox_dynamic_version(dynamic, index, &version);
    if (r != ARTBOX_ELF_OK) return r;
    if (version.name || symbol.section) return ARTBOX_ELF_NOT_FOUND;
#define EXPORT(label, function) if (!strcmp(symbol.name, label)) { *address = (uintptr_t)function; return ARTBOX_ELF_OK; }
    if (!strcmp(dynamic->soname, "libdl_android.so")) {
        EXPORT("__loader_android_get_exported_namespace", loader_namespace)
        return ARTBOX_ELF_NOT_FOUND;
    }
    EXPORT("__loader_android_dlopen_ext", loader_open_ext)
    EXPORT("__loader_dlopen", loader_open)
    EXPORT("__loader_dlerror", loader_error)
    EXPORT("__loader_dlsym", loader_symbol)
    EXPORT("__loader_dlvsym", loader_version)
    EXPORT("__loader_dladdr", loader_address)
    EXPORT("__loader_dlclose", loader_close)
    EXPORT("__loader_dl_iterate_phdr", loader_iterate)
#undef EXPORT
    return ARTBOX_ELF_NOT_FOUND;
}
