#include "artbox/linker.h"
#include "artbox/managed_reference.h"
#include "artbox/native_call.h"
#include "artbox/native_vm.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single-process diagnostic window. A production ART runtime must establish
 * ownership, GC and thread lifetime before using these bridges. */
static artbox_reference_window window;
extern void artbox_reference_delete(void *pointer);
static uint32_t compress(const void *pointer) {
    uint32_t value;
    if (artbox_reference_encode(&window, (uintptr_t)pointer, &value)) abort();
    return value;
}
static void *decompress(uint32_t value) {
    uint64_t pointer;
    if (artbox_reference_decode(&window, value, &pointer)) abort();
    return (void *)(uintptr_t)pointer;
}
static artbox_elf_result resolve(void *context, const artbox_dynamic *dynamic, uint32_t index, uint64_t *out) {
    artbox_elf_symbol symbol;
    (void)context;
    if (artbox_dynamic_symbol(dynamic, index, &symbol) != ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
    if (!strcmp(symbol.name, "artbox_art_reference_compress")) {
        uint32_t (*entry)(const void *) = compress;
        _Static_assert(sizeof(entry) == sizeof(*out), "native address width");
        memcpy(out, &entry, sizeof(entry));
        return ARTBOX_ELF_OK;
    }
    if (!strcmp(symbol.name, "artbox_art_reference_decompress")) {
        void *(*entry)(uint32_t) = decompress;
        _Static_assert(sizeof(entry) == sizeof(*out), "native address width");
        memcpy(out, &entry, sizeof(entry));
        return ARTBOX_ELF_OK;
    }
    if (!strcmp(symbol.name, "_ZdlPv")) {
        void (*entry)(void *) = artbox_reference_delete;
        _Static_assert(sizeof(entry) == sizeof(*out), "native address width");
        memcpy(out, &entry, sizeof(entry));
        return ARTBOX_ELF_OK;
    }
    return ARTBOX_ELF_NOT_FOUND;
}
static artbox_elf_result reject_constructor(void *context, uint64_t address) {
    (void)context; (void)address;
    return ARTBOX_ELF_INVALID;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "ART reference host check failed at %d\n", __LINE__); return 1; } } while (0)
int main(int argc, char **argv) {
    if ((argc != 4 && argc != 5) || (strcmp(argv[3], "plain") && strcmp(argv[3], "poisoned"))) return 2;
    const char *contract = argc == 5 ? argv[4] : "reference";
    int storage = !strcmp(contract, "storage") || !strcmp(contract, "storage-negative");
    int stack = !strcmp(contract, "stack") || !strcmp(contract, "stack-negative");
    int rejection = !strcmp(contract, "storage-negative") || !strcmp(contract, "stack-negative");
    if (!storage && !stack && strcmp(contract, "reference")) return 2;
    int expected_cases = stack ? 18 : storage ? 27 : 19;
    int rejected_case = stack ? 4 : 5;
    const char *entry_name = stack ? "artbox_art_stack_check" :
                            storage ? "artbox_art_storage_check" : "artbox_art_reference_check";
    int poison = !strcmp(argv[3], "poisoned");
    FILE *input = fopen(argv[2], "rb");
    CHECK(input && !fseek(input, 0, SEEK_END));
    long length = ftell(input);
    CHECK(length > 0 && length < 16 * 1024 * 1024 && !fseek(input, 0, SEEK_SET));
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
    CHECK(artbox_load_group_create(&module, 1, module.name, resolve, NULL, &group) == ARTBOX_ELF_OK);
    CHECK(artbox_load_group_relocate(group) == ARTBOX_ELF_OK &&
          artbox_load_group_initialize(group, reject_constructor, NULL) == ARTBOX_ELF_OK);
    uint64_t entry;
    CHECK(artbox_load_group_lookup(group, entry_name, NULL, &entry) == ARTBOX_ELF_OK);
    artbox_elf_symbol symbol;
    CHECK(artbox_dynamic_lookup(&dynamic, entry_name, &symbol) == ARTBOX_ELF_OK &&
          symbol.type == 2 && symbol.value % 4 == 0 && symbol.value < elf.segments[0].file_size &&
          symbol.size <= elf.segments[0].file_size - symbol.value);
    artbox_vm_ops ops = artbox_native_vm();
    size_t span = ops.page_size * 4;
    void *base = NULL;
    CHECK(ops.reserve(span, &base) == 0 && (uintptr_t)base >= UINT64_C(0x100000000));
    /* The argument control must distinguish offsets from truncated pointers. */
    size_t shift = stack && !((uintptr_t)base & UINT64_C(0xffffffff)) ? ops.page_size : 0;
    unsigned char *heap_base = (unsigned char *)base + shift;
    CHECK(!artbox_reference_window_init(&window, (uintptr_t)heap_base, span - shift, ops.page_size));
    unsigned char *first = heap_base + ops.page_size, *second = first + ops.page_size;
    CHECK(!ops.protect(first, ops.page_size * 2, 3));
    uint32_t observations[4] = {0}, first_bits = (uint32_t)ops.page_size, second_bits = first_bits * 2;
    int32_t result = (int32_t)artbox_call7((void *)(uintptr_t)entry, (uintptr_t)first, (uintptr_t)second,
                                         first_bits, second_bits, (uintptr_t)observations, (uintptr_t)(first + 16), 0);
    if (rejection) {
        /* Stop at the unadapted forwarding or argument-classification boundary
         * before touching the externally supplied object storage. */
        CHECK(result == -rejected_case && *(uint64_t *)first == 0 && *(uint64_t *)second == 0);
        CHECK(observations[0] == 0 && observations[1] == 0 && observations[2] == 0 && observations[3] == 0);
    } else {
        if (result != expected_cases) { fprintf(stderr, "Adapted ART reference case: %d\n", result); return 1; }
        CHECK(*(uint64_t *)first == UINT64_C(0x123456789abcdef0) && *(uint64_t *)second == UINT64_C(0xfedcba9876543210));
        uint32_t fourth = stack ? (uint32_t)expected_cases :
                          storage ? ((second_bits >> 3) | UINT32_C(0xc0000000)) : second_bits;
        CHECK(observations[0] == (uint32_t)poison && observations[1] == (poison && !stack ? 0u-first_bits : first_bits) &&
              observations[2] == (poison && !stack ? 0u-second_bits : second_bits) && observations[3] == fourth);
    }
    artbox_load_group_destroy(group);
    CHECK(!dlclose(library) && !ops.release(base, span));
    free(original);
    printf("{\"cases\":%d,\"result\":%d,\"native_base\":%" PRIu64 ",\"encoding\":\"%s\",\"cleanup\":true,"
           "\"expected_rejection\":%s,\"observations\":[%u,%u,%u,%u]}\n",
           rejection ? rejected_case : expected_cases, result, window.base,
           rejection ? (stack ? "reference-only-rejected" : "absolute-rejected") : "heap-relative",
           rejection ? "true" : "false",
           observations[0], observations[1], observations[2], observations[3]);
    return 0;
}
