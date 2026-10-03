// SPDX-License-Identifier: MIT
#include "artbox/linker.h"
#include "artbox/native_call.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "unwind host check failed at %d\n", __LINE__); return 1; } } while (0)
static artbox_elf_result reject_constructor(void *context, uint64_t address) {
    (void)context; (void)address;
    return ARTBOX_ELF_INVALID;
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *input = fopen(argv[2], "rb");
    CHECK(input && !fseek(input, 0, SEEK_END));
    long length = ftell(input);
    CHECK(length > 0 && length < 1024 * 1024 && !fseek(input, 0, SEEK_SET));
    unsigned char *original = malloc((size_t)length);
    CHECK(original && fread(original, 1, (size_t)length, input) == (size_t)length && !fclose(input));
    artbox_elf elf;
    artbox_dynamic dynamic;
    CHECK(artbox_elf_open(original, (size_t)length, &elf) == ARTBOX_ELF_OK && elf.type == 3 &&
          elf.segment_count == 2 && elf.segments[0].virtual_address == 0 &&
          elf.segments[0].flags == 5 && elf.segments[1].flags == 6 &&
          artbox_dynamic_open(&elf, &dynamic) == ARTBOX_ELF_OK && !dynamic.needed_count &&
          !dynamic.init && !dynamic.fini && !dynamic.init_array.size && !dynamic.fini_array.size &&
          !dynamic.preinit_array.size && dynamic.soname);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    unsigned char *rx = dlsym(library, "artbox_dynamic_rx"), *rw = dlsym(library, "artbox_dynamic_rw");
    CHECK(rx && rw && (uintptr_t)rx % 16384 == 0 &&
          (uintptr_t)rw == (uintptr_t)rx + elf.segments[1].virtual_address &&
          !memcmp(rx, original, (size_t)elf.segments[0].file_size) &&
          !memcmp(rw, original + elf.segments[1].file_offset, (size_t)elf.segments[1].file_size));
    for (uint64_t i = elf.segments[1].file_size; i < elf.segments[1].memory_size; ++i) CHECK(rw[i] == 0);
    artbox_relocation_memory memory = {1, rw, (size_t)elf.segments[1].memory_size};
    artbox_link_module module = {dynamic.soname, &dynamic, (uintptr_t)rx, &memory, 1};
    artbox_load_group *group = NULL;
    CHECK(artbox_load_group_create(&module, 1, module.name, NULL, NULL, &group) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(group) == ARTBOX_ELF_OK &&
          artbox_load_group_initialize(group, reject_constructor, NULL) == ARTBOX_ELF_OK);
    uint64_t entry;
    CHECK(artbox_load_group_lookup(group, "artbox_check_unwind_context", NULL, &entry) == ARTBOX_ELF_OK);
    artbox_elf_symbol symbol;
    CHECK(artbox_dynamic_lookup(&dynamic, "artbox_check_unwind_context", &symbol) == ARTBOX_ELF_OK &&
          symbol.type == 2 && symbol.value % 4 == 0 && symbol.value < elf.segments[0].file_size &&
          symbol.size <= elf.segments[0].file_size - symbol.value);
    int32_t result = (int32_t)artbox_call7((void *)(uintptr_t)entry, 0, 0, 0, 0, 0, 0, 0);
    CHECK(result == 0);
    artbox_load_group_destroy(group);
    CHECK(!dlclose(library));
    free(original);
    puts("{\"checks\":4,\"failure_mask\":0,\"expected_rejection\":false,\"cleanup\":true}");
    return 0;
}
