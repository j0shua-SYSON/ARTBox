#include "artbox/elf.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *input;
    unsigned char *bytes;
    long length;
    artbox_elf image;
    artbox_elf_result result;
    unsigned i;
    if (argc != 2) return 2;
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
