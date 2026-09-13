#include "artbox/elf.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}
static void put32(unsigned char *p, unsigned long v) {
    unsigned i; for (i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (i * 8));
}
static void put64(unsigned char *p, uint64_t v) {
    unsigned i; for (i = 0; i < 8; ++i) p[i] = (unsigned char)(v >> (i * 8));
}
static void fixture(unsigned char *data, size_t size) {
    memset(data, 0, size);
    memcpy(data, "\177ELF\2\1\1", 7);
    put16(data + 16, 2); put16(data + 18, 183); put32(data + 20, 1);
    put64(data + 24, 0x4100); put64(data + 32, 64);
    put16(data + 52, 64); put16(data + 54, 56); put16(data + 56, 1);
    put32(data + 64, 1); put32(data + 68, 5);
    put64(data + 72, 0); put64(data + 80, 0x4000);
    put64(data + 96, size); put64(data + 104, size); put64(data + 112, 0x4000);
}

int main(void) {
    unsigned char data[512];
    artbox_elf image;
    size_t length;
    fixture(data, sizeof(data));
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_OK);
    CHECK(image.entry == 0x4100 && image.segment_count == 1);
    CHECK(image.segments[0].file_size == sizeof(data));
    CHECK(image.segments[0].virtual_address == 0x4000);
    CHECK(artbox_elf_validate(NULL, 10, &image) == ARTBOX_ELF_INVALID);
    CHECK(artbox_elf_validate(data, sizeof(data), NULL) == ARTBOX_ELF_INVALID);
    for (length = 0; length < sizeof(data); ++length) {
        CHECK(artbox_elf_validate(data, length, &image) != ARTBOX_ELF_OK);
    }
    data[0] = 0; CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); data[4] = 1;
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); data[5] = 2;
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put16(data + 18, 62);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put16(data + 16, 3);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put64(data + 32, UINT64_MAX - 8);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put16(data + 54, 55);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put16(data + 56, 0);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put32(data + 68, 7);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put64(data + 104, 1);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(data + 104, UINT64_MAX);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(data + 112, 3);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(data + 72, 1);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(data + 24, 0x9000);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put64(data + 24, 0x4101);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    fixture(data, sizeof(data)); put32(data + 64, 3);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put32(data + 64, 2);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put32(data + 64, 7);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_UNSUPPORTED);
    fixture(data, sizeof(data)); put16(data + 56, 2);
    memcpy(data + 120, data + 64, 56);
    CHECK(artbox_elf_validate(data, sizeof(data), &image) == ARTBOX_ELF_INVALID);
    printf("ELF bounds/format/permissions: %u checks PASS\n", checks);
    return 0;
}
