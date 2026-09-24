// SPDX-License-Identifier: MIT
#include "artbox/linker.h"
#include "artbox/native_call.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "math host check failed at %d\n", __LINE__); return 1; } } while (0)
static artbox_elf_result reject_constructor(void *context, uint64_t address) {
    (void)context; (void)address;
    return ARTBOX_ELF_INVALID;
}
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    unsigned char *original[2] = {NULL, NULL};
    void *library[2] = {NULL, NULL};
    artbox_elf elf[2];
    artbox_dynamic dynamic[2];
    artbox_relocation_memory memory[2];
    artbox_link_module module[2];
    const char *names[2] = {"libartbox-math-check.so", "libm.so"};
    for (unsigned i = 0; i < 2; ++i) {
        FILE *input = fopen(argv[2 + i * 2], "rb");
        CHECK(input && !fseek(input, 0, SEEK_END));
        long length = ftell(input);
        CHECK(length > 0 && length < 1024 * 1024 && !fseek(input, 0, SEEK_SET));
        original[i] = malloc((size_t)length);
        CHECK(original[i] && fread(original[i], 1, (size_t)length, input) == (size_t)length && !fclose(input));
        CHECK(artbox_elf_open(original[i], (size_t)length, &elf[i]) == ARTBOX_ELF_OK && elf[i].type == 3 &&
              elf[i].segment_count == 2 && elf[i].segments[0].virtual_address == 0 &&
              elf[i].segments[0].flags == 5 && elf[i].segments[1].flags == 6 &&
              artbox_dynamic_open(&elf[i], &dynamic[i]) == ARTBOX_ELF_OK &&
              !dynamic[i].init && !dynamic[i].fini && !dynamic[i].init_array.size && !dynamic[i].fini_array.size &&
              !dynamic[i].preinit_array.size && dynamic[i].soname && !strcmp(dynamic[i].soname, names[i]));
        CHECK(dynamic[i].needed_count == (i ? 0 : 1));
        library[i] = dlopen(argv[1 + i * 2], RTLD_NOW | RTLD_LOCAL);
        if (!library[i]) { fprintf(stderr, "%s\n", dlerror()); return 1; }
        unsigned char *rx = dlsym(library[i], "artbox_dynamic_rx"), *rw = dlsym(library[i], "artbox_dynamic_rw");
        CHECK(rx && rw && (uintptr_t)rx % 16384 == 0 &&
              (uintptr_t)rw == (uintptr_t)rx + elf[i].segments[1].virtual_address &&
              !memcmp(rx, original[i], (size_t)elf[i].segments[0].file_size) &&
              !memcmp(rw, original[i] + elf[i].segments[1].file_offset, (size_t)elf[i].segments[1].file_size));
        for (uint64_t n = elf[i].segments[1].file_size; n < elf[i].segments[1].memory_size; ++n) CHECK(rw[n] == 0);
        memory[i] = (artbox_relocation_memory){1, rw, (size_t)elf[i].segments[1].memory_size};
        module[i] = (artbox_link_module){names[i], &dynamic[i], (uintptr_t)rx, &memory[i], 1};
    }
    artbox_load_group *group = NULL;
    CHECK(artbox_load_group_create(module, 2, names[0], NULL, NULL, &group) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_count(group) == 2 && artbox_load_group_relocate(group) == ARTBOX_ELF_OK &&
          artbox_load_group_initialize(group, reject_constructor, NULL) == ARTBOX_ELF_OK);
    const char *checks[2] = {"artbox_math_case_count", "artbox_math_check"};
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t entry;
        artbox_elf_symbol symbol;
        CHECK(artbox_load_group_lookup(group, checks[i], NULL, &entry) == ARTBOX_ELF_OK &&
              artbox_dynamic_lookup(&dynamic[0], checks[i], &symbol) == ARTBOX_ELF_OK &&
              symbol.type == 2 && symbol.value % 4 == 0 && symbol.value < elf[0].segments[0].file_size &&
              symbol.size <= elf[0].segments[0].file_size - symbol.value);
        uint32_t observed = (uint32_t)artbox_call7((void *)(uintptr_t)entry, 0, 0, 0, 0, 0, 0, 0);
        if (observed != (i ? 0 : 78)) { fprintf(stderr, "%s returned %u\n", checks[i], observed); return 1; }
    }
    artbox_load_group_destroy(group);
    for (unsigned i = 0; i < 2; ++i) { CHECK(!dlclose(library[i])); free(original[i]); }
    puts("{\"cases\":78,\"first_failure\":0,\"cleanup\":true}");
    return 0;
}
