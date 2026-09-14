#include "artbox/relocation.h"
#include <stdlib.h>
#include <string.h>

#define RELOCATION_LIMIT (1024u * 1024u)
enum { R_NONE = 0, R_ABS64 = 257, R_GLOB_DAT = 1025, R_JUMP_SLOT = 1026, R_RELATIVE = 1027, R_TLSDESC = 1031 };
typedef struct write_plan {
    unsigned char *target;
    uint64_t address, value, argument;
    unsigned width;
    uint32_t type, symbol;
} write_plan;
typedef struct preparation {
    const artbox_dynamic *dynamic;
    const artbox_relocation_memory *memory;
    unsigned memory_count;
    write_plan *writes;
    size_t count;
    artbox_relocation_stats stats;
} preparation;
static uint64_t u64(const unsigned char *p) {
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}
static void p64(unsigned char *p, uint64_t value) {
    unsigned i;
    for (i = 0; i < 8; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t av = (uintptr_t)a, bv = (uintptr_t)b;
    if (!an || !bn) return 0;
    return av <= bv ? bv - av < an : av - bv < bn;
}
static int table_valid(const artbox_dynamic *d, const artbox_elf_table *table, unsigned width) {
    const void *bytes = NULL;
    if (table->size % width) return 0;
    if (!table->size) return 1;
    return artbox_elf_virtual_span(d->image, table->address, table->size, &bytes) == ARTBOX_ELF_OK &&
           bytes == table->data;
}
static artbox_elf_result check_memory(const preparation *p) {
    const artbox_elf *image = p->dynamic->image;
    unsigned i, j;
    if (!image || !image->data || image->segment_count > ARTBOX_ELF_MAX_SEGMENTS ||
        p->memory_count > image->segment_count || (p->memory_count && !p->memory) ||
        image->size > UINTPTR_MAX - (uintptr_t)image->data) return ARTBOX_ELF_INVALID;
    for (i = 0; i < p->memory_count; ++i) {
        const artbox_relocation_memory *m = &p->memory[i];
        const artbox_elf_segment *s;
        if (m->segment_index >= image->segment_count || !m->data || !m->size ||
            m->size > UINTPTR_MAX - (uintptr_t)m->data) return ARTBOX_ELF_INVALID;
        s = &image->segments[m->segment_index];
        if ((s->flags & 3) != 2 || s->memory_size != m->size ||
            s->memory_size > UINT64_MAX - s->virtual_address ||
            overlaps(m->data, m->size, image->data, image->size)) return ARTBOX_ELF_INVALID;
        for (j = 0; j < i; ++j) {
            const artbox_relocation_memory *other = &p->memory[j];
            if (other->segment_index == m->segment_index ||
                overlaps(m->data, m->size, other->data, other->size)) return ARTBOX_ELF_INVALID;
        }
    }
    return ARTBOX_ELF_OK;
}
static unsigned char *target(const preparation *p, uint64_t address, unsigned width) {
    unsigned i;
    if (address > UINT64_MAX - width) return NULL;
    for (i = 0; i < p->memory_count; ++i) {
        const artbox_relocation_memory *m = &p->memory[i];
        uint64_t start = p->dynamic->image->segments[m->segment_index].virtual_address;
        if (address >= start && m->size >= width && address - start <= m->size - width)
            return m->data + (size_t)(address - start);
    }
    return NULL;
}
static artbox_elf_result add_write(preparation *p, uint64_t address, uint32_t type,
                                    uint32_t symbol, uint64_t addend, int implicit) {
    write_plan *w;
    unsigned char *where;
    if (type == R_NONE) return ARTBOX_ELF_OK;
    if (type != R_ABS64 && type != R_GLOB_DAT && type != R_JUMP_SLOT && type != R_RELATIVE && type != R_TLSDESC)
        return ARTBOX_ELF_UNSUPPORTED;
    if (symbol >= p->dynamic->symbol_count || (type == R_RELATIVE && symbol)) return ARTBOX_ELF_INVALID;
    where = target(p, address, type == R_TLSDESC ? 16 : 8);
    if (!where) return ARTBOX_ELF_INVALID;
    w = &p->writes[p->count++];
    w->target = where; w->address = address; w->type = type; w->symbol = symbol;
    w->width = type == R_TLSDESC ? 16 : 8; w->argument = 0;
    w->value = implicit ? u64(where) : addend;
    return ARTBOX_ELF_OK;
}
static artbox_elf_result prepare_rela(preparation *p, const artbox_elf_table *table, size_t *count) {
    uint64_t offset;
    for (offset = 0; offset < table->size; offset += 24) {
        const unsigned char *entry = table->data + (size_t)offset;
        uint64_t info = u64(entry + 8);
        artbox_elf_result error = add_write(p, u64(entry), (uint32_t)info, (uint32_t)(info >> 32),
                                          u64(entry + 16), 0);
        if (error != ARTBOX_ELF_OK) return error;
        if ((uint32_t)info != R_NONE) ++*count;
    }
    return ARTBOX_ELF_OK;
}
static artbox_elf_result prepare_relr(preparation *p) {
    const artbox_elf_table *table = &p->dynamic->relr;
    uint64_t offset, cursor = 0;
    int has_address = 0;
    for (offset = 0; offset < table->size; offset += 8) {
        uint64_t entry = u64(table->data + (size_t)offset);
        artbox_elf_result error;
        if (!(entry & 1)) {
            if (entry > UINT64_MAX - 8) return ARTBOX_ELF_INVALID;
            error = add_write(p, entry, R_RELATIVE, 0, 0, 1);
            if (error != ARTBOX_ELF_OK) return error;
            ++p->stats.relr_count; cursor = entry + 8; has_address = 1;
        } else {
            unsigned bit;
            if (!has_address || cursor > UINT64_MAX - 63 * 8) return ARTBOX_ELF_INVALID;
            for (bit = 1; bit < 64; ++bit) if (entry & (UINT64_C(1) << bit)) {
                error = add_write(p, cursor + (uint64_t)(bit - 1) * 8, R_RELATIVE, 0, 0, 1);
                if (error != ARTBOX_ELF_OK) return error;
                ++p->stats.relr_count;
            }
            cursor += 63 * 8;
        }
    }
    return ARTBOX_ELF_OK;
}
static int by_address(const void *a, const void *b) {
    uint64_t av = ((const write_plan *)a)->address, bv = ((const write_plan *)b)->address;
    return av < bv ? -1 : av > bv;
}
static int defined_range(const artbox_elf *image, const artbox_elf_symbol *symbol) {
    unsigned i;
    if (symbol->section == 0xfff1) return 1; /* SHN_ABS. */
    for (i = 0; i < image->segment_count; ++i) {
        const artbox_elf_segment *s = &image->segments[i];
        if (symbol->value >= s->virtual_address && symbol->value - s->virtual_address <= s->memory_size &&
            symbol->size <= s->memory_size - (symbol->value - s->virtual_address)) return 1;
    }
    return 0;
}
static artbox_elf_result symbol_value(const artbox_dynamic *d, uint64_t bias, uint32_t index,
                                    artbox_relocation_resolver resolve, void *context, uint64_t *out) {
    artbox_elf_symbol symbol;
    artbox_elf_result error;
    if (!index) { *out = 0; return ARTBOX_ELF_OK; }
    error = artbox_dynamic_symbol(d, index, &symbol);
    if (error != ARTBOX_ELF_OK) return error;
    if (symbol.type > 2 || symbol.binding > 2 ||
        (symbol.section >= 0xff00 && symbol.section != 0xfff1)) return ARTBOX_ELF_UNSUPPORTED;
    if (symbol.section && !defined_range(d->image, &symbol)) return ARTBOX_ELF_INVALID;
    if (!symbol.section && (!symbol.binding || symbol.visibility)) return ARTBOX_ELF_INVALID;
    if (symbol.binding && !symbol.visibility && resolve) {
        error = resolve(context, d, index, out);
        if (error != ARTBOX_ELF_NOT_FOUND) return error;
    }
    if (symbol.section) {
        *out = symbol.value + (symbol.section == 0xfff1 ? 0 : bias);
        return ARTBOX_ELF_OK;
    }
    if (symbol.binding == 2) { *out = 0; return ARTBOX_ELF_OK; }
    return ARTBOX_ELF_NOT_FOUND;
}
artbox_elf_result artbox_relocate_tls(const artbox_dynamic *dynamic, uint64_t load_bias,
    const artbox_relocation_memory *memory, unsigned memory_count,
    artbox_relocation_resolver resolve, artbox_tlsdesc_binding tls, void *context, artbox_relocation_stats *stats) {
    preparation p;
    uint64_t capacity, rela_count, relr_words;
    size_t i;
    artbox_elf_result error;
    if (!dynamic || !dynamic->symbol_count) return ARTBOX_ELF_INVALID;
    memset(&p, 0, sizeof(p)); p.dynamic = dynamic; p.memory = memory; p.memory_count = memory_count;
    error = check_memory(&p);
    if (error != ARTBOX_ELF_OK) return error;
    if (!table_valid(dynamic, &dynamic->rela, 24) || !table_valid(dynamic, &dynamic->plt_rela, 24) ||
        !table_valid(dynamic, &dynamic->relr, 8)) return ARTBOX_ELF_INVALID;
    rela_count = dynamic->rela.size / 24 + dynamic->plt_rela.size / 24;
    relr_words = dynamic->relr.size / 8;
    if (rela_count > RELOCATION_LIMIT || relr_words > RELOCATION_LIMIT / 63) return ARTBOX_ELF_UNSUPPORTED;
    capacity = rela_count + relr_words * 63;
    if (capacity > RELOCATION_LIMIT || capacity > SIZE_MAX / sizeof(write_plan)) return ARTBOX_ELF_UNSUPPORTED;
    if (capacity) {
        p.writes = malloc((size_t)capacity * sizeof(write_plan));
        if (!p.writes) return ARTBOX_ELF_NO_MEMORY;
    }
    error = prepare_relr(&p);
    if (error != ARTBOX_ELF_OK) goto done;
    error = prepare_rela(&p, &dynamic->rela, &p.stats.rela_count);
    if (error != ARTBOX_ELF_OK) goto done;
    error = prepare_rela(&p, &dynamic->plt_rela, &p.stats.plt_count);
    if (error != ARTBOX_ELF_OK) goto done;
    if (p.count) qsort(p.writes, p.count, sizeof(write_plan), by_address);
    for (i = 1; i < p.count; ++i) if (p.writes[i].address - p.writes[i - 1].address < p.writes[i - 1].width) {
        error = ARTBOX_ELF_UNSUPPORTED; goto done;
    }
    for (i = 0; i < p.count; ++i) {
        write_plan *w = &p.writes[i];
        uint64_t value = load_bias;
        if (w->type == R_TLSDESC) {
            if (!tls) { error = ARTBOX_ELF_UNSUPPORTED; goto done; }
            if (w->symbol) {
                artbox_elf_symbol symbol;
                error = artbox_dynamic_symbol(dynamic, w->symbol, &symbol);
                if (error != ARTBOX_ELF_OK) goto done;
                if (symbol.type != 6 || symbol.binding > 2 || symbol.section >= 0xff00) {
                    error = ARTBOX_ELF_UNSUPPORTED; goto done;
                }
                if ((!symbol.section && (!symbol.binding || symbol.visibility)) ||
                    (symbol.section && (!dynamic->image->has_tls || symbol.value > dynamic->image->tls.memory_size ||
                     symbol.size > dynamic->image->tls.memory_size - symbol.value))) {
                    error = ARTBOX_ELF_INVALID; goto done;
                }
            }
            error = tls(context, dynamic, w->symbol, w->value, &value, &w->argument);
            if (error != ARTBOX_ELF_OK) goto done;
            if (!value || (value & 3)) { error = ARTBOX_ELF_INVALID; goto done; }
            w->value = value;
            continue;
        }
        if (w->type != R_RELATIVE) {
            error = symbol_value(dynamic, load_bias, w->symbol, resolve, context, &value);
            if (error != ARTBOX_ELF_OK) goto done;
        }
        /* AAELF64 ABS64 and pointer-sized dynamic operations retain bits 63:0. */
        w->value += value;
    }
    for (i = 0; i < p.count; ++i) {
        p64(p.writes[i].target, p.writes[i].value);
        if (p.writes[i].width == 16) p64(p.writes[i].target + 8, p.writes[i].argument);
    }
    if (stats) *stats = p.stats;
done:
    free(p.writes);
    return error;
}
artbox_elf_result artbox_relocate(const artbox_dynamic *dynamic, uint64_t load_bias,
    const artbox_relocation_memory *memory, unsigned memory_count,
    artbox_relocation_resolver resolve, void *context, artbox_relocation_stats *stats) {
    return artbox_relocate_tls(dynamic, load_bias, memory, memory_count, resolve, NULL, context, stats);
}
