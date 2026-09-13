#include "artbox/elf.h"
#include "artbox/dynamic.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void quoted(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    if (!p) { printf("null"); return; }
    putchar('"');
    while (*p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32 || *p >= 127) printf("\\u%04x", (unsigned)*p);
        else putchar(*p);
        ++p;
    }
    putchar('"');
}

static int inspect_dynamic(const artbox_elf *image) {
    artbox_dynamic d;
    artbox_elf_result result = artbox_dynamic_open(image, &d);
    uint32_t i;
    if (result != ARTBOX_ELF_OK) {
        fprintf(stderr, "%s\n", artbox_elf_result_string(result)); return 1;
    }
    /* Check lookup before emitting output, including every named export. */
    for (i = 1; i < d.symbol_count; ++i) {
        artbox_elf_symbol symbol, found;
        if (artbox_dynamic_symbol(&d, i, &symbol) != ARTBOX_ELF_OK) return 1;
        if (symbol.section && (symbol.binding == 1 || symbol.binding == 2 || symbol.binding == 10) &&
            (symbol.visibility == 0 || symbol.visibility == 3) &&
            (artbox_dynamic_lookup(&d, symbol.name, &found) != ARTBOX_ELF_OK || found.value != symbol.value))
            return 1;
    }
    printf("{\"needed\":[");
    for (i = 0; i < d.needed_count; ++i) { if (i) putchar(','); quoted(d.needed[i]); }
    printf("],\"soname\":"); quoted(d.soname);
    printf(",\"rela_bytes\":%" PRIu64 ",\"plt_rela_bytes\":%" PRIu64 ",\"relr_bytes\":%" PRIu64
           ",\"init_array_bytes\":%" PRIu64 ",\"symbols\":[", d.rela.size, d.plt_rela.size, d.relr.size, d.init_array.size);
    for (i = 0; i < d.symbol_count; ++i) {
        artbox_elf_symbol symbol;
        if (artbox_dynamic_symbol(&d, i, &symbol) != ARTBOX_ELF_OK) return 1;
        if (i) putchar(',');
        printf("{\"name\":"); quoted(symbol.name);
        printf(",\"value\":%" PRIu64 ",\"size\":%" PRIu64 ",\"section\":%u,\"binding\":%u,\"type\":%u,\"visibility\":%u}",
               symbol.value, symbol.size, (unsigned)symbol.section, (unsigned)symbol.binding,
               (unsigned)symbol.type, (unsigned)symbol.visibility);
    }
    printf("]}\n");
    return 0;
}

int main(int argc, char **argv) {
    FILE *input;
    unsigned char *bytes;
    long length;
    artbox_elf image;
    artbox_elf_result result;
    unsigned i;
    if (argc != 2 && (argc != 3 || strcmp(argv[2], "--dynamic") != 0)) return 2;
#if defined(_MSC_VER)
    if (fopen_s(&input, argv[1], "rb") != 0) return 2;
#else
    if (!(input = fopen(argv[1], "rb"))) return 2;
#endif
    if (fseek(input, 0, SEEK_END) || (length = ftell(input)) <= 0 || length > 64 * 1024 * 1024 ||
        fseek(input, 0, SEEK_SET)) { fclose(input); return 2; }
    bytes = malloc((size_t)length);
    if (!bytes) { fclose(input); return 2; }
    if (fread(bytes, 1, (size_t)length, input) != (size_t)length) {
        free(bytes); fclose(input); return 2;
    }
    fclose(input);
    result = artbox_elf_open(bytes, (size_t)length, &image);
    if (result != ARTBOX_ELF_OK) {
        fprintf(stderr, "%s\n", artbox_elf_result_string(result)); free(bytes); return 1;
    }
    if (argc == 3) {
        int status = inspect_dynamic(&image);
        free(bytes); return status;
    }
    printf("{\"type\":%u,\"entry\":%" PRIu64 ",\"phnum\":%u,\"dynamic\":%u,\"tls\":%u,\"relro\":%u,\"loads\":[",
           (unsigned)image.type, image.entry, (unsigned)image.program_header_count,
           image.has_dynamic, image.has_tls, image.has_relro);
    for (i = 0; i < image.segment_count; ++i) {
        const artbox_elf_segment *p = &image.segments[i];
        printf("%s{\"offset\":%" PRIu64 ",\"vaddr\":%" PRIu64 ",\"filesz\":%" PRIu64
               ",\"memsz\":%" PRIu64 ",\"align\":%" PRIu64 ",\"flags\":%u}",
               i ? "," : "", p->file_offset, p->virtual_address, p->file_size,
               p->memory_size, p->alignment, p->flags);
    }
    printf("]}\n");
    free(bytes);
    return 0;
}
