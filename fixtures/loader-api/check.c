// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stddef.h>
#include <stdint.h>

#define PROVIDER "libartbox_loader_provider.so"
#define CLIENT "libartbox_loader_client.so"
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)

static int equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int suffix(const char *name, const char *tail) {
    if (!name) return 0;
    size_t n = 0, t = 0;
    while (name[n]) ++n;
    while (tail[t]) ++t;
    return n >= t && equal(name + n - t, tail);
}
int artbox_loader_value(void) { return 101; }

struct iteration {
    void *handle, *symbol, *base;
    unsigned found, fields;
};
static int visit(struct dl_phdr_info *info, size_t size, void *opaque) {
    struct iteration *s = opaque;
    if (size < offsetof(struct dl_phdr_info, dlpi_phnum) + sizeof(info->dlpi_phnum)) return -1;
    if (!suffix(info->dlpi_name, PROVIDER)) return 0;
    ++s->found;
    if (info->dlpi_addr == (uintptr_t)s->base) s->fields |= 1;
    if (info->dlpi_phdr && info->dlpi_phnum) s->fields |= 2;
    if (info->dlpi_phdr) for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = info->dlpi_phdr + i;
        uintptr_t first = info->dlpi_addr + p->p_vaddr, address = (uintptr_t)s->symbol;
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && address >= first && address - first < p->p_memsz)
            s->fields |= 4;
    }
    // A callback may reenter the linker. It must retain the explicit scope.
    if (dlsym(s->handle, "artbox_loader_value") == s->symbol) s->fields |= 8;
    return 37;
}

int artbox_loader_check(void) {
    int cases = 0;
    while (dlerror()) {}
    CHECK(dlerror() == NULL);
    void *handle = dlopen(PROVIDER, RTLD_NOW | RTLD_LOCAL);
    CHECK(handle != NULL);
    void *value = dlsym(handle, "artbox_loader_value");
    CHECK(value != NULL);
    CHECK(((int (*)(void))value)() == 202);
    void *root = dlsym(RTLD_DEFAULT, "artbox_loader_value");
    CHECK(root != NULL);
    CHECK(((int (*)(void))root)() == 101);
    void *next = dlsym(RTLD_NEXT, "artbox_loader_value");
    CHECK(next != NULL);
    CHECK(((int (*)(void))next)() == 202);
    CHECK(dlsym(handle, "artbox_loader_check") == NULL);
    const char *error = dlerror();
    CHECK(error && *error);
    CHECK(dlerror() == NULL);
    CHECK(dlopen("libartbox_loader_absent.so", RTLD_NOW) == NULL);
    error = dlerror();
    CHECK(error && *error);
    CHECK(dlerror() == NULL);
    CHECK(dlopen("/artbox-no-such-directory/" PROVIDER, RTLD_NOW) == NULL);
    CHECK(dlerror() != NULL && dlerror() == NULL);
    Dl_info location = {0};
    CHECK(dladdr(value, &location) != 0);
    CHECK(suffix(location.dli_fname, PROVIDER));
    CHECK(location.dli_fbase != NULL);
    CHECK(equal(location.dli_sname, "artbox_loader_value"));
    CHECK(location.dli_saddr == value);
    struct iteration state = {handle, value, location.dli_fbase, 0, 0};
    CHECK(dladdr(root, &location) != 0);
    CHECK(suffix(location.dli_fname, CLIENT));
    CHECK(location.dli_saddr == root);
    CHECK(dl_iterate_phdr(visit, &state) == 37);
    CHECK(state.found == 1 && state.fields == 15);
    void *again = dlopen(PROVIDER, RTLD_NOW | RTLD_NOLOAD);
    CHECK(again != NULL);
    CHECK(again == handle);
    CHECK(dlclose(again) == 0);
    CHECK(dlclose(handle) == 0);
    CHECK(dlsym(RTLD_DEFAULT, "artbox_loader_absent_symbol") == NULL);
    CHECK(dlerror() != NULL && dlerror() == NULL);
    return cases;
}

// Each thread owns its pending error. The harness interleaves these stages.
int artbox_loader_error_stage(unsigned stage) {
    if (stage == 0) {
        while (dlerror()) {}
        return dlsym(RTLD_DEFAULT, "artbox_loader_thread_missing") == NULL ? 0 : -1;
    }
    if (stage == 1) {
        const char *error = dlerror();
        return error && *error && dlerror() == NULL ? 0 : -2;
    }
    if (stage == 2) return dlerror() == NULL ? 0 : -3;
    return -4;
}
