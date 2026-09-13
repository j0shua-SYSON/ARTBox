/* Data-only integration harness. Its fixed two-image lookup scope is a test
 * fixture, not the Android runtime's dependency graph or execution loader. */
#include "artbox/relocation.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct module {
    unsigned char *file, *original;
    artbox_elf image;
    artbox_dynamic dynamic;
    artbox_relocation_memory memory[ARTBOX_ELF_MAX_SEGMENTS];
    unsigned memory_count;
    uint64_t bias;
    artbox_relocation_stats stats;
} module;
static void release(module *m) {
    unsigned i;
    for (i = 0; i < m->memory_count; ++i) free(m->memory[i].data);
    free(m->file); free(m->original);
}
static int load(const char *path, module *m) {
    FILE *file;
    long length;
    unsigned i;
    uint64_t allocated = 0;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb")) return 0;
#else
    if (!(file = fopen(path, "rb"))) return 0;
#endif
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || length > 64 * 1024 * 1024 ||
        fseek(file, 0, SEEK_SET)) { fclose(file); return 0; }
    m->file = malloc((size_t)length); m->original = malloc((size_t)length);
    if (!m->file || !m->original) { fclose(file); return 0; }
    if (fread(m->file, 1, (size_t)length, file) != (size_t)length) { fclose(file); return 0; }
    fclose(file); memcpy(m->original, m->file, (size_t)length);
    if (artbox_elf_open(m->file, (size_t)length, &m->image) != ARTBOX_ELF_OK ||
        artbox_dynamic_open(&m->image, &m->dynamic) != ARTBOX_ELF_OK) return 0;
    for (i = 0; i < m->image.segment_count; ++i) {
        const artbox_elf_segment *s = &m->image.segments[i];
        artbox_relocation_memory *v;
        if (!(s->flags & 2) || !s->memory_size) continue;
        if (s->memory_size > 64 * 1024 * 1024 - allocated) return 0;
        allocated += s->memory_size;
        v = &m->memory[m->memory_count++];
        v->segment_index = i; v->size = (size_t)s->memory_size; v->data = calloc(1, v->size);
        if (!v->data) return 0;
        memcpy(v->data, m->file + (size_t)s->file_offset, (size_t)s->file_size);
    }
    return 1;
}
static artbox_elf_result resolve(void *context, const artbox_dynamic *d, uint32_t index, uint64_t *value) {
    module *modules = context;
    artbox_elf_symbol requested;
    unsigned i;
    artbox_elf_result error = artbox_dynamic_symbol(d, index, &requested);
    if (error != ARTBOX_ELF_OK) return error;
    for (i = 0; i < 2; ++i) {
        artbox_elf_symbol defined;
        error = artbox_dynamic_lookup(&modules[i].dynamic, requested.name, &defined);
        if (error == ARTBOX_ELF_NOT_FOUND) continue;
        if (error != ARTBOX_ELF_OK) return error;
        if (defined.type > 2 || (defined.section >= 0xff00 && defined.section != 0xfff1))
            return ARTBOX_ELF_UNSUPPORTED;
        *value = defined.value + (defined.section == 0xfff1 ? 0 : modules[i].bias);
        return ARTBOX_ELF_OK;
    }
    return ARTBOX_ELF_NOT_FOUND;
}
int main(int argc, char **argv) {
    module modules[2];
    unsigned i;
    int status = 1;
    memset(modules, 0, sizeof(modules));
    if (argc != 3) return 2;
    for (i = 0; i < 2; ++i) {
        modules[i].bias = (uint64_t)(i + 1) * 0x10000000;
        if (!load(argv[i + 1], &modules[i])) goto done;
    }
    for (i = 0; i < 2; ++i) {
        module *m = &modules[i];
        artbox_elf_result error = artbox_relocate(&m->dynamic, m->bias, m->memory, m->memory_count,
                                                 resolve, modules, &m->stats);
        if (error != ARTBOX_ELF_OK) {
            fprintf(stderr, "%s\n", artbox_elf_result_string(error)); goto done;
        }
        if (memcmp(m->file, m->original, m->image.size)) goto done;
    }
    printf("{\"bias\":%" PRIu64 ",\"rela\":%zu,\"plt\":%zu,\"relr\":%zu,\"segments\":[",
           modules[0].bias, modules[0].stats.rela_count, modules[0].stats.plt_count, modules[0].stats.relr_count);
    for (i = 0; i < modules[0].memory_count; ++i) {
        const artbox_relocation_memory *m = &modules[0].memory[i];
        size_t offset;
        printf("%s{\"index\":%u,\"bytes\":\"", i ? "," : "", m->segment_index);
        for (offset = 0; offset < m->size; ++offset) printf("%02x", (unsigned)m->data[offset]);
        printf("\"}");
    }
    printf("]}\n"); status = 0;
done:
    for (i = 0; i < 2; ++i) release(&modules[i]);
    return status;
}
