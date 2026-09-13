#include "artbox/dynamic.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)
static void p16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void p32(unsigned char *p, uint32_t v) { unsigned i; for (i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (i * 8)); }
static void p64(unsigned char *p, uint64_t v) { unsigned i; for (i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (i * 8)); }
static void tag(unsigned char *p, unsigned i, uint64_t key, uint64_t value) {
    p64(p + 0x800 + i * 16, key); p64(p + 0x808 + i * 16, value);
}
static void program(unsigned char *p, unsigned type, unsigned flags, uint64_t offset,
                    uint64_t address, uint64_t file, uint64_t memory, uint64_t align) {
    p32(p, type); p32(p + 4, flags); p64(p + 8, offset); p64(p + 16, address);
    p64(p + 32, file); p64(p + 40, memory); p64(p + 48, align);
}
static void fixture(unsigned char *p, size_t size) {
    static const char names[] = "\0dep.so\0self.so\0value\0missing\0";
    memset(p, 0, size); memcpy(p, "\177ELF\2\1\1", 7);
    p16(p + 16, 3); p16(p + 18, 183); p32(p + 20, 1);
    p64(p + 32, 64); p16(p + 52, 64); p16(p + 54, 56); p16(p + 56, 3);
    program(p + 64, 1, 5, 0, 0x10000, 2048, 2048, 16384);
    program(p + 120, 1, 6, 2048, 0x14800, 2048, 4096, 2048);
    program(p + 176, 2, 6, 2048, 0x14800, 23 * 16, 23 * 16, 8);
    memcpy(p + 0x300, names, sizeof(names));
    /* Null symbol, undefined import, then one exported object. */
    p32(p + 0x418, 22); p[0x41c] = 0x12;
    p32(p + 0x430, 16); p[0x434] = 0x11; p16(p + 0x436, 1);
    p64(p + 0x438, 0x14a00); p64(p + 0x440, 8);
    /* SysV: one bucket containing symbol 2. */
    p32(p + 0x500, 1); p32(p + 0x504, 3); p32(p + 0x508, 2);
    /* GNU: hash("value") = 0x108d5742; one bloom word and one chain. */
    p32(p + 0x540, 1); p32(p + 0x544, 2); p32(p + 0x548, 1); p32(p + 0x54c, 5);
    p64(p + 0x550, UINT64_C(0x0400000000000004)); p32(p + 0x558, 2); p32(p + 0x55c, 0x108d5743);
    p64(p + 0x600, 0x14a20); p64(p + 0x608, UINT64_C(2) << 32 | 257); p64(p + 0x610, 3);
    p64(p + 0x640, 0x14a28);
    p64(p + 0x680, 0x14a30); p64(p + 0x688, UINT64_C(1) << 32 | 1026);
    p64(p + 0xb00, 0x100f0);
    tag(p, 0, 5, 0x10300); tag(p, 1, 10, sizeof(names)); tag(p, 2, 6, 0x10400); tag(p, 3, 11, 24);
    tag(p, 4, 0x6ffffef5, 0x10540); tag(p, 5, 4, 0x10500); tag(p, 6, 1, 1); tag(p, 7, 14, 8);
    tag(p, 8, 7, 0x10600); tag(p, 9, 8, 24); tag(p, 10, 9, 24);
    tag(p, 11, 36, 0x10640); tag(p, 12, 35, 8); tag(p, 13, 37, 8);
    tag(p, 14, 23, 0x10680); tag(p, 15, 2, 24); tag(p, 16, 20, 7);
    tag(p, 17, 12, 0x100f0); tag(p, 18, 25, 0x14b00); tag(p, 19, 27, 8);
    tag(p, 20, 30, 8); tag(p, 21, 0x6ffffffb, 1);
}
static artbox_elf_result parse(unsigned char *data, artbox_elf *image, artbox_dynamic *dynamic) {
    artbox_elf_result result = artbox_elf_open(data, 4096, image);
    return result == ARTBOX_ELF_OK ? artbox_dynamic_open(image, dynamic) : result;
}

int main(void) {
    unsigned char data[4096];
    artbox_elf image;
    artbox_dynamic dynamic, saved;
    artbox_elf_symbol symbol;
    unsigned i;
    fixture(data, sizeof(data));
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_OK);
    CHECK(dynamic.needed_count == 1 && strcmp(dynamic.needed[0], "dep.so") == 0);
    CHECK(strcmp(dynamic.soname, "self.so") == 0 && dynamic.symbol_count == 3);
    CHECK(dynamic.rela.size == 24 && dynamic.plt_rela.size == 24 && dynamic.relr.size == 8);
    CHECK(dynamic.init == 0x100f0 && dynamic.init_array.address == 0x14b00 && dynamic.init_array.size == 8);
    CHECK(dynamic.flags == 8 && dynamic.flags_1 == 1);
    CHECK(artbox_dynamic_symbol(&dynamic, 1, &symbol) == ARTBOX_ELF_OK);
    CHECK(strcmp(symbol.name, "missing") == 0 && symbol.section == 0 && symbol.type == 2);
    CHECK(artbox_dynamic_lookup(&dynamic, "value", &symbol) == ARTBOX_ELF_OK);
    CHECK(symbol.value == 0x14a00 && symbol.size == 8 && symbol.binding == 1 && symbol.type == 1);
    CHECK(artbox_dynamic_lookup(&dynamic, "missing", &symbol) == ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_dynamic_lookup(&dynamic, "absent", &symbol) == ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_dynamic_symbol(&dynamic, 3, &symbol) == ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_dynamic_lookup(NULL, "value", &symbol) == ARTBOX_ELF_INVALID);
    CHECK(artbox_dynamic_lookup(&dynamic, NULL, &symbol) == ARTBOX_ELF_INVALID);
    CHECK(artbox_dynamic_lookup(&dynamic, "value", NULL) == ARTBOX_ELF_INVALID);
    tag(data, 4, 21, 0); /* SysV-only input. */
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_OK);
    CHECK(artbox_dynamic_lookup(&dynamic, "value", &symbol) == ARTBOX_ELF_OK);
    fixture(data, sizeof(data)); tag(data, 5, 21, 0); /* GNU-only input. */
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_OK && dynamic.symbol_count == 3);
    CHECK(artbox_dynamic_lookup(&dynamic, "value", &symbol) == ARTBOX_ELF_OK);
    fixture(data, sizeof(data)); p64(data + 0x550, 0);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); p32(data + 0x514, 2); /* SysV self-cycle. */
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); p32(data + 0x55c, 0x108d5741);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); p32(data + 0x558, 1);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); p32(data + 0x548, 3);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); tag(data, 20, 5, 0x10300);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); tag(data, 0, 5, UINT64_MAX);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); tag(data, 3, 11, 16);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); tag(data, 9, 8, 23);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); tag(data, 12, 35, 7);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); tag(data, 16, 20, 17);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); tag(data, 20, 30, 4);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); tag(data, 20, 0x60000011, 0x10600);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); tag(data, 20, 0x6ffffff0, 0x10700);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); p32(data + 0x430, 4096);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); data[0x435] = 0x80; /* AArch64 variant PCS. */
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); tag(data, 6, 1, 0);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data));
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_OK); saved = dynamic;
    for (i = 0; i < 23; ++i) tag(data, i, 0x60000100, 1);
    CHECK(parse(data, &image, &dynamic) == ARTBOX_ELF_INVALID);
    CHECK(memcmp(&dynamic, &saved, sizeof(saved)) == 0);
    printf("Dynamic tables, GNU/SysV lookup and rejected formats: %u checks PASS\n", checks);
    return 0;
}
