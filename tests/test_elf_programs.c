#include "artbox/elf.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { unsigned i; for (i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (i * 8)); }
static void put64(unsigned char *p, uint64_t v) { unsigned i; for (i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (i * 8)); }
static unsigned char *ph(unsigned char *p, unsigned index) { return p + 64 + index * 56; }
static void program(unsigned char *p, uint32_t type, uint32_t flags, uint64_t offset,
                    uint64_t address, uint64_t file, uint64_t memory, uint64_t align) {
    put32(p, type); put32(p + 4, flags); put64(p + 8, offset); put64(p + 16, address);
    put64(p + 32, file); put64(p + 40, memory); put64(p + 48, align);
}
static void fixture(unsigned char *p, size_t size) {
    memset(p, 0, size); memcpy(p, "\177ELF\2\1\1", 7);
    put16(p + 16, 3); put16(p + 18, 183); put32(p + 20, 1);
    put64(p + 32, 64); put16(p + 52, 64); put16(p + 54, 56); put16(p + 56, 5);
    program(ph(p, 0), 1, 5, 0, 0x10000, 1024, 1024, 16384);
    program(ph(p, 1), 1, 6, 1024, 0x14400, 1024, 2048, 1024);
    program(ph(p, 2), 2, 6, 1024, 0x14400, 128, 128, 8);
    program(ph(p, 3), 7, 4, 1408, 0x14580, 16, 32, 16);
    program(ph(p, 4), 0x6474e552, 4, 1024, 0x14400, 256, 256, 1);
    memset(p + 1408, 0x5a, 16);
}

int main(void) {
    unsigned char data[2048];
    artbox_elf image, unchanged;
    const void *span = NULL;
    static const char interpreter[] = "/system/bin/linker64";
    fixture(data, sizeof(data));
    memset(&image, 0x5a, sizeof(image)); unchanged = image;
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    CHECK(memcmp(&image, &unchanged, sizeof(image)) == 0);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_OK);
    CHECK(image.type == 3 && image.entry == 0 && image.segment_count == 2);
    CHECK(image.program_header_offset == 64 && image.program_header_count == 5);
    CHECK(image.has_dynamic && image.dynamic.virtual_address == 0x14400 && image.dynamic.file_size == 128);
    CHECK(image.has_tls && image.tls.file_size == 16 && image.tls.memory_size == 32 && image.tls.alignment == 16);
    CHECK(image.has_relro && image.relro.virtual_address == 0x14400 && image.relro.memory_size == 256);
    CHECK(image.interpreter == NULL);
    CHECK(artbox_elf_virtual_span(&image, 0x14580, 16, &span) == ARTBOX_ELF_OK);
    CHECK(span == data + 1408 && ((const unsigned char *)span)[0] == 0x5a);
    CHECK(artbox_elf_virtual_span(&image, 0x14800, 1, &span) == ARTBOX_ELF_NOT_FOUND);
    CHECK(span == data + 1408);
    CHECK(artbox_elf_virtual_span(&image, 0x103ff, 2, &span) == ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_elf_virtual_span(&image, UINT64_MAX, 2, &span) == ARTBOX_ELF_NOT_FOUND);
    CHECK(artbox_elf_virtual_span(NULL, 0, 1, &span) == ARTBOX_ELF_INVALID);
    CHECK(artbox_elf_virtual_span(&image, 0x10000, 1, NULL) == ARTBOX_ELF_INVALID);
    put16(data + 56, 6);
    program(ph(data, 5), 3, 4, 800, 0x10320, sizeof(interpreter), sizeof(interpreter), 1);
    memcpy(data + 800, interpreter, sizeof(interpreter));
    put64(data + 24, 0x10300);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_OK);
    CHECK(strcmp(image.interpreter, interpreter) == 0 && image.entry == 0x10300);
    data[800 + sizeof(interpreter) - 1] = 'x';
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put16(data + 16, 2); put64(data + 24, 0x10300);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_OK);
    put64(data + 24, 0);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(data + 24, 0x14400);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(ph(data, 2) + 8, 1040);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(ph(data, 2) + 32, 127);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(ph(data, 2) + 16, 0x14800);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put32(ph(data, 4), 2);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put32(ph(data, 4), 7);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(ph(data, 3) + 48, 3);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(ph(data, 3) + 40, 0x10000);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(ph(data, 4) + 40, 0x10000);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put32(ph(data, 1) + 4, 7);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put32(ph(data, 4), 0x6474e551); put32(ph(data, 4) + 4, 7);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put16(data + 16, 1);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); unchanged = image; put64(data + 32, UINT64_MAX);
    CHECK(artbox_elf_open(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    CHECK(memcmp(&image, &unchanged, sizeof(image)) == 0);
    printf("ELF dynamic program headers and borrowed ranges: %u checks PASS\n", checks);
    return 0;
}
