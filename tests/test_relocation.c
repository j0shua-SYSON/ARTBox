#include "artbox/relocation.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)
static void p64(unsigned char *p, uint64_t v) {
    unsigned i; for (i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
static uint64_t u64(const unsigned char *p) {
    uint64_t v = 0; unsigned i; for (i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i); return v;
}
typedef struct fixture {
    unsigned char file[1024], memory[2048], original[2048];
    artbox_elf image;
    artbox_dynamic dynamic;
    artbox_relocation_memory mapping;
    artbox_relocation_stats stats;
    unsigned calls;
    artbox_elf_result resolved;
    uint64_t address;
} fixture;
static void rela(fixture *f, unsigned index, uint64_t address, uint32_t symbol,
                 uint32_t type, uint64_t addend) {
    unsigned char *p = f->file + 128 + index * 24;
    p64(p, address); p64(p + 8, (uint64_t)symbol << 32 | type); p64(p + 16, addend);
}
static void symbol(fixture *f, unsigned binding, unsigned type, unsigned visibility,
                   unsigned section, uint64_t value) {
    unsigned char *p = f->file + 512 + 24;
    memset(p, 0, 24); p[0] = 1; p[4] = (unsigned char)(binding << 4 | type);
    p[5] = (unsigned char)visibility; p[6] = (unsigned char)section; p[7] = (unsigned char)(section >> 8);
    p64(p + 8, value);
}
static void reset(fixture *f) {
    memset(f, 0, sizeof(*f));
    f->image.data = f->file; f->image.size = sizeof(f->file); f->image.segment_count = 2;
    f->image.segments[0] = (artbox_elf_segment){0x1000, 1024, 0, 1024, 1, 5};
    f->image.segments[1] = (artbox_elf_segment){0x4000, 2048, 1024, 0, 1, 6};
    f->dynamic.image = &f->image;
    f->dynamic.strings = "\0external\0"; f->dynamic.strings_size = 10;
    f->dynamic.symbols = f->file + 512; f->dynamic.symbol_count = 2;
    f->dynamic.rela = (artbox_elf_table){f->file + 128, 0x1080, 24};
    f->mapping = (artbox_relocation_memory){1, f->memory, sizeof(f->memory)};
    f->resolved = ARTBOX_ELF_OK; f->address = 0x9000;
    symbol(f, 1, 1, 0, 0, 0);
    rela(f, 0, 0x4000, 0, 1027, 0x1234);
}
static artbox_elf_result resolve(void *opaque, const artbox_dynamic *dynamic,
                                uint32_t index, uint64_t *address) {
    fixture *f = opaque;
    if (dynamic != &f->dynamic || index != 1) return ARTBOX_ELF_INVALID;
    ++f->calls; *address = f->address; return f->resolved;
}
static artbox_elf_result apply(fixture *f) {
    return artbox_relocate(&f->dynamic, 0x10000000, &f->mapping, 1, resolve, f, &f->stats);
}
static artbox_elf_result tls_binding(void *opaque, const artbox_dynamic *d,
    uint32_t index, uint64_t addend, uint64_t *entry, uint64_t *argument) {
    fixture *f = opaque;
    if (d != &f->dynamic || index != 1 || addend != 7) return ARTBOX_ELF_INVALID;
    ++f->calls; *entry = 0x8000; *argument = 0x9000; return f->resolved;
}
static int tls_tests(void) {
    fixture f;
    reset(&f); symbol(&f, 1, 6, 0, 0, 0); rela(&f, 0, 0x4000, 1, 1031, 7);
    CHECK(artbox_relocate_tls(&f.dynamic, 0, &f.mapping, 1, resolve, tls_binding, &f, &f.stats) == ARTBOX_ELF_OK);
    CHECK(u64(f.memory) == 0x8000 && u64(f.memory + 8) == 0x9000 && f.calls == 1 && f.stats.rela_count == 1);
    /* Descriptor writes cover sixteen bytes: overlap and a truncated second
     * word fail before binding, with no partially published resolver pointer. */
    for (unsigned mode = 0; mode < 3; ++mode) {
        reset(&f); symbol(&f, 1, 6, 0, 0, 0); rela(&f, 0, 0x4000, 1, 1031, 7);
        if (mode == 0) { f.dynamic.rela.size = 48; rela(&f, 1, 0x4008, 0, 1027, 0); }
        if (mode == 1) rela(&f, 0, 0x47f8, 1, 1031, 7);
        if (mode == 2) f.resolved = ARTBOX_ELF_NOT_FOUND;
        memset(&f.stats, 0xa5, sizeof(f.stats));
        artbox_relocation_stats old = f.stats;
        memcpy(f.original, f.memory, sizeof(f.memory));
        CHECK(artbox_relocate_tls(&f.dynamic, 0, &f.mapping, 1, resolve, tls_binding, &f, &f.stats) != ARTBOX_ELF_OK);
        CHECK(!memcmp(f.original, f.memory, sizeof(f.memory)) && !memcmp(&old, &f.stats, sizeof(old)));
        CHECK(f.calls == (mode == 2 ? 1u : 0u));
    }
    return 0;
}
static int rejected(fixture *f, artbox_elf_result expected) {
    artbox_relocation_stats before;
    memset(&f->stats, 0xa5, sizeof(f->stats)); before = f->stats;
    memcpy(f->original, f->memory, sizeof(f->memory));
    CHECK(apply(f) == expected);
    CHECK(memcmp(f->memory, f->original, sizeof(f->memory)) == 0);
    CHECK(memcmp(&before, &f->stats, sizeof(before)) == 0);
    return 0;
}
int main(void) {
    fixture f;
    unsigned i;
    CHECK(tls_tests() == 0);
    reset(&f);
    CHECK(apply(&f) == ARTBOX_ELF_OK);
    CHECK(u64(f.memory) == 0x10001234 && f.stats.rela_count == 1 && f.calls == 0);
    CHECK(f.stats.relr_count == 0 && f.stats.plt_count == 0);
    /* AArch64 writes the low 64 bits, including negative addends and wrap. */
    reset(&f); rela(&f, 0, 0x4001, 0, 1027, UINT64_MAX);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory + 1) == 0x0fffffff);
    reset(&f); rela(&f, 0, 0x4000, 0, 257, UINT64_MAX);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == UINT64_MAX && f.calls == 0);
    for (i = 0; i < 3; ++i) {
        static const unsigned types[] = {257, 1025, 1026};
        reset(&f); rela(&f, 0, 0x4000, 1, types[i], UINT64_MAX - 3);
        CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == 0x8ffc && f.calls == 1);
    }
    reset(&f); f.dynamic.plt_rela = f.dynamic.rela; f.dynamic.rela.size = 0;
    rela(&f, 0, 0x4008, 1, 1026, 4);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory + 8) == 0x9004 && f.stats.plt_count == 1);
    /* Resolve a default definition through the scope; bind protected/local definitions here. */
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 4); symbol(&f, 1, 1, 0, 1, 0x4020);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == 0x9004 && f.calls == 1);
    for (i = 1; i <= 3; ++i) {
        reset(&f); rela(&f, 0, 0x4000, 1, 257, 4); symbol(&f, 1, 1, i, 1, 0x4020);
        CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == 0x10004024 && f.calls == 0);
    }
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 4); symbol(&f, 0, 1, 0, 1, 0x4020);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == 0x10004024 && f.calls == 0);
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 4); symbol(&f, 0, 0, 0, 0xfff1, 0x55);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == 0x59);
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 4); symbol(&f, 2, 1, 0, 0, 0);
    f.resolved = ARTBOX_ELF_NOT_FOUND;
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory) == 4);
    /* A missing later import must not leave earlier relative writes applied. */
    reset(&f); f.dynamic.rela.size = 48; rela(&f, 1, 0x4008, 1, 257, 0);
    f.resolved = ARTBOX_ELF_NOT_FOUND;
    CHECK(rejected(&f, ARTBOX_ELF_NOT_FOUND) == 0);
    f.resolved = ARTBOX_ELF_UNSUPPORTED;
    CHECK(rejected(&f, ARTBOX_ELF_UNSUPPORTED) == 0);
    /* RELR: direct word, high and low bitmap bits, a second bitmap, then another direct word. */
    reset(&f); f.dynamic.rela.size = 0;
    f.dynamic.relr = (artbox_elf_table){f.file + 256, 0x1100, 32};
    p64(f.file + 256, 0x4000); p64(f.file + 264, UINT64_C(0x8000000000000003));
    p64(f.file + 272, 3); p64(f.file + 280, 0x4500);
    p64(f.memory, 0x1111); p64(f.memory + 8, 0x2222);
    p64(f.memory + 504, 0x3333); p64(f.memory + 512, 0x4444); p64(f.memory + 1280, 0x5555);
    CHECK(apply(&f) == ARTBOX_ELF_OK && f.stats.relr_count == 5);
    CHECK(u64(f.memory) == 0x10001111 && u64(f.memory + 8) == 0x10002222);
    CHECK(u64(f.memory + 504) == 0x10003333 && u64(f.memory + 512) == 0x10004444);
    CHECK(u64(f.memory + 1280) == 0x10005555 && u64(f.memory + 16) == 0);
    reset(&f); f.dynamic.relr = (artbox_elf_table){f.file + 256, 0x1100, 8}; p64(f.file + 256, 3);
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    /* Empty bitmap still advances its 63-word window. Even unaligned direct offsets are legal. */
    reset(&f); f.dynamic.rela.size = 0;
    f.dynamic.relr = (artbox_elf_table){f.file + 256, 0x1100, 24};
    p64(f.file + 256, 0x4002); p64(f.file + 264, 1); p64(f.file + 272, 3);
    CHECK(apply(&f) == ARTBOX_ELF_OK && u64(f.memory + 2) == 0x10000000);
    CHECK(u64(f.memory + 514) == 0x10000000 && u64(f.memory + 10) == 0);
    /* Signed text, unmapped gaps, crossing a segment end, and absent mappings. */
    for (i = 0; i < 4; ++i) {
        static const uint64_t addresses[] = {0x1000, 0x3000, 0x47fc, UINT64_MAX - 3};
        reset(&f); rela(&f, 0, addresses[i], 0, 1027, 0);
        CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    }
    reset(&f); --f.mapping.size;
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    reset(&f); f.mapping.segment_index = 0;
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    reset(&f); f.mapping.data = f.file;
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    reset(&f); f.dynamic.rela.size = 48; rela(&f, 1, 0x4004, 0, 1027, 1);
    CHECK(rejected(&f, ARTBOX_ELF_UNSUPPORTED) == 0);
    reset(&f); f.dynamic.relr = (artbox_elf_table){f.file + 256, 0x1100, 8}; p64(f.file + 256, 0x4000);
    CHECK(rejected(&f, ARTBOX_ELF_UNSUPPORTED) == 0);
    reset(&f); rela(&f, 0, 0x4000, 1, 1027, 0);
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    reset(&f); rela(&f, 0, 0x4000, 2, 257, 0);
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    for (i = 0; i < 5; ++i) {
        static const unsigned types[] = {1024, 1028, 1031, 1032, 283};
        reset(&f); rela(&f, 0, 0x4000, 0, types[i], 0);
        CHECK(rejected(&f, ARTBOX_ELF_UNSUPPORTED) == 0);
    }
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 0); symbol(&f, 1, 6, 0, 1, 0);
    CHECK(rejected(&f, ARTBOX_ELF_UNSUPPORTED) == 0);
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 0); symbol(&f, 1, 10, 0, 1, 0x1000);
    CHECK(rejected(&f, ARTBOX_ELF_UNSUPPORTED) == 0);
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 0); symbol(&f, 0, 1, 0, 1, 0x5000);
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    reset(&f); rela(&f, 0, 0x4000, 1, 257, 0); symbol(&f, 1, 1, 2, 0, 0);
    CHECK(rejected(&f, ARTBOX_ELF_INVALID) == 0);
    reset(&f); rela(&f, 0, UINT64_MAX, UINT32_MAX, 0, 0);
    CHECK(apply(&f) == ARTBOX_ELF_OK && f.stats.rela_count == 0 && f.calls == 0);
    CHECK(artbox_relocate(NULL, 0, NULL, 0, NULL, NULL, NULL) == ARTBOX_ELF_INVALID);
    printf("%u relocation checks passed\n", checks);
    return 0;
}
