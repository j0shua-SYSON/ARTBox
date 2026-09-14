#include "artbox/dynamic.h"
#include <string.h>

#define SYMBOL_LIMIT (1024u * 1024u)
enum key { STR, STRSZ, SYM, SYMENT, HASH, GNU_HASH, RELA, RELASZ, RELAENT,
           RELR, RELRSZ, RELRENT, JMPREL, PLTRELSZ, PLTREL, INIT, FINI,
           INIT_ARRAY, INIT_ARRAYSZ, FINI_ARRAY, FINI_ARRAYSZ, PREINIT_ARRAY,
           PREINIT_ARRAYSZ, SONAME, FLAGS, FLAGS_1, RPATH, RUNPATH, PLTGOT,
           VERSYM, VERDEF, VERDEFNUM, VERNEED, VERNEEDNUM, KEY_COUNT };
static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const unsigned char *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static const unsigned char *view(const artbox_elf *image, uint64_t address, uint64_t length) {
    const void *data = NULL;
    return artbox_elf_virtual_span(image, address, length, &data) == ARTBOX_ELF_OK ? data : NULL;
}
static uint64_t available(const artbox_elf *image, uint64_t address) {
    unsigned i;
    for (i = 0; i < image->segment_count; ++i) {
        const artbox_elf_segment *s = &image->segments[i];
        if (address >= s->virtual_address && address - s->virtual_address < s->file_size)
            return s->file_size - (address - s->virtual_address);
    }
    return 0;
}
static const char *string_at(const artbox_dynamic *d, uint64_t offset) {
    if (offset >= d->strings_size || !memchr(d->strings + (size_t)offset, 0, d->strings_size - (size_t)offset))
        return NULL;
    return d->strings + (size_t)offset;
}
static uint32_t gnu_hash(const char *name) {
    uint32_t hash = 5381;
    while (*name) hash = hash * 33 + (unsigned char)*name++;
    return hash;
}
static uint32_t sysv_hash(const char *name) {
    uint32_t hash = 0;
    while (*name) {
        uint32_t high;
        hash = (hash << 4) + (unsigned char)*name++;
        high = hash & UINT32_C(0xf0000000);
        hash = (hash ^ (high >> 24)) & ~high;
    }
    return hash;
}
static uint64_t bloom_bits(uint32_t hash, uint32_t shift) {
    return (UINT64_C(1) << (hash % 64)) | (UINT64_C(1) << ((hash >> shift) % 64));
}
static int tag_key(uint64_t tag) {
    switch (tag) {
    case 5: return STR; case 10: return STRSZ; case 6: return SYM; case 11: return SYMENT;
    case 4: return HASH; case 0x6ffffef5: return GNU_HASH;
    case 7: return RELA; case 8: return RELASZ; case 9: return RELAENT;
    case 36: case 0x6fffe000: return RELR;
    case 35: case 0x6fffe001: return RELRSZ;
    case 37: case 0x6fffe003: return RELRENT;
    case 23: return JMPREL; case 2: return PLTRELSZ; case 20: return PLTREL;
    case 12: return INIT; case 13: return FINI; case 25: return INIT_ARRAY; case 27: return INIT_ARRAYSZ;
    case 26: return FINI_ARRAY; case 28: return FINI_ARRAYSZ;
    case 32: return PREINIT_ARRAY; case 33: return PREINIT_ARRAYSZ;
    case 14: return SONAME; case 30: return FLAGS; case 0x6ffffffb: return FLAGS_1;
    case 15: return RPATH; case 29: return RUNPATH; case 3: return PLTGOT;
    case 0x6ffffff0: return VERSYM; case 0x6ffffffc: return VERDEF; case 0x6ffffffd: return VERDEFNUM;
    case 0x6ffffffe: return VERNEED; case 0x6fffffff: return VERNEEDNUM;
    default: return -1;
    }
}

static const artbox_elf_version *version_index(const artbox_dynamic *d, unsigned index) {
    for (unsigned i = 0; i < d->version_count; ++i)
        if (d->versions[i].index == index) return &d->versions[i];
    return NULL;
}
static artbox_elf_result add_version(artbox_dynamic *d, uint16_t index, uint16_t flags,
                                    uint32_t hash, const char *name, const char *file) {
    artbox_elf_version record = {0};
    if (!name || !*name || !index || index > 0x7fff || (file && index < 2) ||
        (flags & ~(file ? 2u : 3u)) || (!file && ((flags & 1) != (index == 1))) ||
        sysv_hash(name) != hash || version_index(d, index)) return ARTBOX_ELF_INVALID;
    if (d->version_count == ARTBOX_ELF_MAX_VERSIONS) return ARTBOX_ELF_UNSUPPORTED;
    record.index = index; record.flags = flags; record.hash = hash; record.name = name; record.file = file;
    d->versions[d->version_count++] = record;
    return ARTBOX_ELF_OK;
}
static artbox_elf_result versions_open(artbox_dynamic *d, const uint64_t *values, const unsigned char *seen) {
    if (seen[VERDEF] != seen[VERDEFNUM] || seen[VERNEED] != seen[VERNEEDNUM] ||
        ((seen[VERDEF] || seen[VERNEED]) && !seen[VERSYM])) return ARTBOX_ELF_INVALID;
    if (!seen[VERSYM]) return ARTBOX_ELF_OK;
    d->versym = view(d->image, values[VERSYM], (uint64_t)d->symbol_count * 2);
    if (!d->versym || u16(d->versym)) return ARTBOX_ELF_INVALID;
    for (unsigned kind = 0; kind < 2; ++kind) {
        int key = kind ? VERNEED : VERDEF, count_key = kind ? VERNEEDNUM : VERDEFNUM;
        if (!seen[key]) continue;
        uint64_t address = values[key], count = values[count_key];
        unsigned header_size = kind ? 16 : 20, aux_size = kind ? 16 : 8;
        if (!count) return ARTBOX_ELF_INVALID;
        if (count > ARTBOX_ELF_MAX_VERSIONS) return ARTBOX_ELF_UNSUPPORTED;
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t capacity = available(d->image, address);
            const unsigned char *header = view(d->image, address, header_size);
            if (!header) return ARTBOX_ELF_INVALID;
            if (u16(header) != 1) return ARTBOX_ELF_UNSUPPORTED;
            uint32_t next = u32(header + (kind ? 12 : 16));
            if ((i + 1 == count) != (next == 0) || (next && (next < header_size || next > capacity || next % 4)))
                return ARTBOX_ELF_INVALID;
            if (next) capacity = next;
            unsigned aux_count = u16(header + (kind ? 2 : 6));
            uint64_t aux = u32(header + (kind ? 8 : 12));
            if (!aux_count) return ARTBOX_ELF_INVALID;
            if (aux_count > ARTBOX_ELF_MAX_VERSIONS) return ARTBOX_ELF_UNSUPPORTED;
            const char *file = NULL;
            if (kind) {
                file = string_at(d, u32(header + 4));
                int found = 0;
                for (unsigned n = 0; file && n < d->needed_count; ++n)
                    if (!strcmp(file, d->needed[n])) found = 1;
                if (!found) return ARTBOX_ELF_INVALID;
            }
            for (unsigned j = 0; j < aux_count; ++j) {
                if (aux < header_size || aux % 4 || aux > capacity || aux_size > capacity - aux)
                    return ARTBOX_ELF_INVALID;
                const unsigned char *item = view(d->image, address + aux, aux_size);
                if (!item) return ARTBOX_ELF_INVALID;
                const char *name = string_at(d, u32(item + (kind ? 8 : 0)));
                if (!name || !*name) return ARTBOX_ELF_INVALID;
                if (kind || !j) {
                    artbox_elf_result error = add_version(d, u16(kind ? item + 6 : header + 4),
                        u16(kind ? item + 4 : header + 2), u32(kind ? item : header + 8), name, file);
                    if (error != ARTBOX_ELF_OK) return error;
                }
                uint32_t aux_next = u32(item + (kind ? 12 : 4));
                if ((j + 1 == aux_count) != (aux_next == 0) ||
                    (aux_next && (aux_next < aux_size || aux_next > capacity - aux))) return ARTBOX_ELF_INVALID;
                aux += aux_next;
            }
            if (next > UINT64_MAX - address) return ARTBOX_ELF_INVALID;
            address += next;
        }
    }
    for (uint32_t i = 1; i < d->symbol_count; ++i) {
        unsigned index = u16(d->versym + (size_t)i * 2) & 0x7fff;
        if (index > 1) {
            const artbox_elf_version *version = version_index(d, index);
            artbox_elf_symbol symbol;
            if (!version || artbox_dynamic_symbol(d, i, &symbol) != ARTBOX_ELF_OK ||
                (!!symbol.section == !!version->file)) return ARTBOX_ELF_INVALID;
        }
    }
    return ARTBOX_ELF_OK;
}

artbox_elf_result artbox_dynamic_version(const artbox_dynamic *d, uint32_t index, artbox_elf_version *out) {
    artbox_elf_version version = {0};
    if (!d || !out) return ARTBOX_ELF_INVALID;
    if (index >= d->symbol_count) return ARTBOX_ELF_NOT_FOUND;
    unsigned raw = d->versym ? u16(d->versym + (size_t)index * 2) : index ? 1u : 0u;
    if ((raw & 0x7fff) > 1) {
        const artbox_elf_version *record = version_index(d, raw & 0x7fff);
        if (!record) return ARTBOX_ELF_INVALID;
        version = *record;
    }
    version.index = (uint16_t)(raw & 0x7fff); version.hidden = !!(raw & 0x8000);
    *out = version; return ARTBOX_ELF_OK;
}

artbox_elf_result artbox_dynamic_symbol(const artbox_dynamic *d, uint32_t index, artbox_elf_symbol *out) {
    const unsigned char *p;
    artbox_elf_symbol symbol;
    if (!d || !out || !d->symbols || !d->strings) return ARTBOX_ELF_INVALID;
    if (index >= d->symbol_count) return ARTBOX_ELF_NOT_FOUND;
    p = d->symbols + (size_t)index * 24;
    if (p[5] & ~3u) return ARTBOX_ELF_UNSUPPORTED; /* Includes AArch64 variant PCS. */
    memset(&symbol, 0, sizeof(symbol));
    symbol.name = string_at(d, u32(p));
    symbol.binding = p[4] >> 4; symbol.type = p[4] & 15; symbol.visibility = p[5] & 3;
    symbol.section = (uint16_t)((uint16_t)p[6] | (uint16_t)p[7] << 8);
    symbol.value = u64(p + 8); symbol.size = u64(p + 16);
    if (!symbol.name || symbol.size > UINT64_MAX - symbol.value) return ARTBOX_ELF_INVALID;
    *out = symbol;
    return ARTBOX_ELF_OK;
}

static artbox_elf_result gnu_shape(artbox_dynamic *d, uint64_t address, uint32_t *count) {
    const unsigned char *p = view(d->image, address, 16);
    uint64_t prefix, length, chain_capacity;
    uint32_t i, highest = 0, symbol;
    if (!p) return ARTBOX_ELF_INVALID;
    d->gnu_bucket_count = u32(p); d->gnu_symbol_offset = u32(p + 4);
    d->gnu_bloom_count = u32(p + 8); d->gnu_shift = u32(p + 12);
    if (!d->gnu_bucket_count || !d->gnu_symbol_offset || !d->gnu_bloom_count || d->gnu_shift >= 32 ||
        (d->gnu_bloom_count & (d->gnu_bloom_count - 1))) return ARTBOX_ELF_INVALID;
    if (d->gnu_bucket_count > SYMBOL_LIMIT || d->gnu_symbol_offset > SYMBOL_LIMIT ||
        d->gnu_bloom_count > SYMBOL_LIMIT) return ARTBOX_ELF_UNSUPPORTED;
    prefix = 16 + (uint64_t)d->gnu_bloom_count * 8 + (uint64_t)d->gnu_bucket_count * 4;
    length = available(d->image, address);
    p = view(d->image, address, length);
    if (!p || prefix > length) return ARTBOX_ELF_INVALID;
    d->gnu_bloom = p + 16;
    d->gnu_buckets = d->gnu_bloom + (size_t)d->gnu_bloom_count * 8;
    d->gnu_chains = p + (size_t)prefix;
    chain_capacity = (length - prefix) / 4;
    for (i = 0; i < d->gnu_bucket_count; ++i) {
        symbol = u32(d->gnu_buckets + (size_t)i * 4);
        if (symbol && symbol < d->gnu_symbol_offset) return ARTBOX_ELF_INVALID;
        if (symbol > highest) highest = symbol;
    }
    if (!highest) { *count = d->gnu_symbol_offset; return ARTBOX_ELF_OK; }
    symbol = highest;
    for (;;) {
        uint32_t hash;
        if (symbol >= SYMBOL_LIMIT || symbol - d->gnu_symbol_offset >= chain_capacity) return ARTBOX_ELF_INVALID;
        hash = u32(d->gnu_chains + (size_t)(symbol - d->gnu_symbol_offset) * 4);
        ++symbol;
        if (hash & 1) break;
    }
    *count = symbol;
    return ARTBOX_ELF_OK;
}

static artbox_elf_result validate_hashes(const artbox_dynamic *d) {
    uint32_t i;
    artbox_elf_symbol symbol;
    for (i = 0; i < d->symbol_count; ++i) {
        artbox_elf_result error = artbox_dynamic_symbol(d, i, &symbol);
        if (error != ARTBOX_ELF_OK) return error;
    }
    if (d->sysv_bucket_count) {
        for (i = 0; i < d->symbol_count; ++i)
            if (u32(d->sysv_chains + (size_t)i * 4) >= d->symbol_count) return ARTBOX_ELF_INVALID;
        if (u32(d->sysv_chains) != 0) return ARTBOX_ELF_INVALID;
        for (i = 0; i < d->sysv_bucket_count; ++i) {
            uint32_t index = u32(d->sysv_buckets + (size_t)i * 4), steps = 0;
            while (index) {
                if (++steps >= d->symbol_count || index >= d->symbol_count ||
                    artbox_dynamic_symbol(d, index, &symbol) != ARTBOX_ELF_OK ||
                    sysv_hash(symbol.name) % d->sysv_bucket_count != i) return ARTBOX_ELF_INVALID;
                index = u32(d->sysv_chains + (size_t)index * 4);
            }
        }
    }
    if (d->gnu_bucket_count) {
        uint32_t covered = 0;
        for (i = 0; i < d->gnu_bucket_count; ++i) {
            uint32_t index = u32(d->gnu_buckets + (size_t)i * 4);
            if (!index) continue;
            for (;;) {
                uint32_t hash, chain;
                uint64_t mask, word;
                if (index < d->gnu_symbol_offset || index >= d->symbol_count ||
                    artbox_dynamic_symbol(d, index, &symbol) != ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
                hash = gnu_hash(symbol.name);
                chain = u32(d->gnu_chains + (size_t)(index - d->gnu_symbol_offset) * 4);
                mask = bloom_bits(hash, d->gnu_shift);
                word = u64(d->gnu_bloom + (size_t)((hash / 64) % d->gnu_bloom_count) * 8);
                if ((hash & ~1u) != (chain & ~1u) || hash % d->gnu_bucket_count != i ||
                    (word & mask) != mask) return ARTBOX_ELF_INVALID;
                ++covered; ++index;
                if (chain & 1) break;
            }
        }
        if (covered != d->symbol_count - d->gnu_symbol_offset) return ARTBOX_ELF_INVALID;
    }
    return ARTBOX_ELF_OK;
}

static int executable(const artbox_elf *image, uint64_t address) {
    unsigned i;
    if (!address) return 1;
    if (address & 3) return 0;
    for (i = 0; i < image->segment_count; ++i) {
        const artbox_elf_segment *s = &image->segments[i];
        if ((s->flags & 5) == 5 && s->file_size >= 4 && address >= s->virtual_address &&
            address - s->virtual_address <= s->file_size - 4) return 1;
    }
    return 0;
}

artbox_elf_result artbox_dynamic_open(const artbox_elf *image, artbox_dynamic *out) {
    artbox_dynamic d;
    uint64_t values[KEY_COUNT] = {0}, needed[ARTBOX_ELF_MAX_NEEDED], offset;
    unsigned char seen[KEY_COUNT] = {0};
    unsigned needed_count = 0, i;
    int terminated = 0;
    const unsigned char *entries;
    artbox_elf_result error;
    struct table_rule { int address, size, entry; unsigned width; artbox_elf_table *out; } tables[6];
    if (!image || !out || !image->data || image->segment_count > ARTBOX_ELF_MAX_SEGMENTS) return ARTBOX_ELF_INVALID;
    if (!image->has_dynamic) return ARTBOX_ELF_NOT_FOUND;
    if (image->dynamic.file_size / 16 > 4096) return ARTBOX_ELF_UNSUPPORTED;
    entries = view(image, image->dynamic.virtual_address, image->dynamic.file_size);
    if (!entries || image->dynamic.file_size % 16) return ARTBOX_ELF_INVALID;
    memset(&d, 0, sizeof(d)); d.image = image;
    for (offset = 0; offset < image->dynamic.file_size; offset += 16) {
        uint64_t tag = u64(entries + (size_t)offset), value = u64(entries + (size_t)offset + 8);
        int key;
        if (!tag) { terminated = 1; break; }
        if (tag == 1) {
            if (needed_count == ARTBOX_ELF_MAX_NEEDED) return ARTBOX_ELF_UNSUPPORTED;
            needed[needed_count++] = value;
            continue;
        }
        if (tag == 22 || tag == 17 || tag == 18 || tag == 19 ||
            (tag >= 0x6000000f && tag <= 0x60000012)) return ARTBOX_ELF_UNSUPPORTED;
        if (tag == 16) { d.flags |= 2; continue; }
        if (tag == 24) { d.flags |= 8; continue; }
        key = tag_key(tag);
        if (key < 0) continue;
        if (seen[key]) return ARTBOX_ELF_INVALID;
        seen[key] = 1; values[key] = value;
    }
    if (!terminated) return ARTBOX_ELF_INVALID;
    if (!seen[STR] || !seen[STRSZ] || !seen[SYM] || !seen[SYMENT] || (!seen[HASH] && !seen[GNU_HASH]))
        return ARTBOX_ELF_UNSUPPORTED;
    if (!values[STRSZ] || values[STRSZ] > SIZE_MAX || values[SYMENT] != 24) return ARTBOX_ELF_INVALID;
    d.strings = (const char *)view(image, values[STR], values[STRSZ]); d.strings_size = (size_t)values[STRSZ];
    if (!d.strings || d.strings[0] || d.strings[d.strings_size - 1]) return ARTBOX_ELF_INVALID;
    for (i = 0; i < needed_count; ++i) {
        const char *name = string_at(&d, needed[i]);
        if (!name || !name[0]) return ARTBOX_ELF_INVALID;
        d.needed[d.needed_count++] = name;
    }
    if (seen[SONAME] && (!(d.soname = string_at(&d, values[SONAME])) || !d.soname[0])) return ARTBOX_ELF_INVALID;
    if (seen[RPATH] && !(d.rpath = string_at(&d, values[RPATH]))) return ARTBOX_ELF_INVALID;
    if (seen[RUNPATH] && !(d.runpath = string_at(&d, values[RUNPATH]))) return ARTBOX_ELF_INVALID;
    d.flags |= values[FLAGS]; d.flags_1 = values[FLAGS_1];
    if (d.flags & 4) return ARTBOX_ELF_UNSUPPORTED;
    if (seen[HASH]) {
        const unsigned char *p = view(image, values[HASH], 8);
        uint64_t size;
        if (!p) return ARTBOX_ELF_INVALID;
        d.sysv_bucket_count = u32(p); d.symbol_count = u32(p + 4);
        if (!d.sysv_bucket_count || !d.symbol_count) return ARTBOX_ELF_INVALID;
        if (d.sysv_bucket_count > SYMBOL_LIMIT || d.symbol_count > SYMBOL_LIMIT) return ARTBOX_ELF_UNSUPPORTED;
        size = 8 + ((uint64_t)d.sysv_bucket_count + d.symbol_count) * 4;
        p = view(image, values[HASH], size);
        if (!p) return ARTBOX_ELF_INVALID;
        d.sysv_buckets = p + 8; d.sysv_chains = d.sysv_buckets + (size_t)d.sysv_bucket_count * 4;
    }
    if (seen[GNU_HASH]) {
        uint32_t count = 0;
        error = gnu_shape(&d, values[GNU_HASH], &count);
        if (error != ARTBOX_ELF_OK) return error;
        if (d.symbol_count && d.symbol_count != count) return ARTBOX_ELF_INVALID;
        d.symbol_count = count;
    }
    d.symbols = view(image, values[SYM], (uint64_t)d.symbol_count * 24);
    if (!d.symbols) return ARTBOX_ELF_INVALID;
    for (i = 0; i < 24; ++i) if (d.symbols[i]) return ARTBOX_ELF_INVALID;
    error = validate_hashes(&d);
    if (error != ARTBOX_ELF_OK) return error;
    error = versions_open(&d, values, seen);
    if (error != ARTBOX_ELF_OK) return error;
    tables[0] = (struct table_rule){RELA, RELASZ, RELAENT, 24, &d.rela};
    tables[1] = (struct table_rule){RELR, RELRSZ, RELRENT, 8, &d.relr};
    tables[2] = (struct table_rule){JMPREL, PLTRELSZ, -1, 24, &d.plt_rela};
    tables[3] = (struct table_rule){INIT_ARRAY, INIT_ARRAYSZ, -1, 8, &d.init_array};
    tables[4] = (struct table_rule){FINI_ARRAY, FINI_ARRAYSZ, -1, 8, &d.fini_array};
    tables[5] = (struct table_rule){PREINIT_ARRAY, PREINIT_ARRAYSZ, -1, 8, &d.preinit_array};
    for (i = 0; i < 6; ++i) {
        const struct table_rule *rule = &tables[i];
        if (rule->entry >= 0 && seen[rule->entry] && values[rule->entry] != rule->width) return ARTBOX_ELF_INVALID;
        if (!seen[rule->address] && !seen[rule->size]) continue;
        if (!seen[rule->address] || !seen[rule->size] || values[rule->size] % rule->width ||
            (rule->entry >= 0 && !seen[rule->entry])) return ARTBOX_ELF_INVALID;
        rule->out->address = values[rule->address]; rule->out->size = values[rule->size];
        if (rule->out->size) {
            rule->out->data = view(image, rule->out->address, rule->out->size);
            if (!rule->out->data) return ARTBOX_ELF_INVALID;
        }
    }
    if (seen[JMPREL] && (!seen[PLTREL] || values[PLTREL] != 7)) return ARTBOX_ELF_UNSUPPORTED;
    d.init = values[INIT]; d.fini = values[FINI]; d.plt_got = values[PLTGOT];
    if (!executable(image, d.init) || !executable(image, d.fini)) return ARTBOX_ELF_INVALID;
    if (seen[PLTGOT] && !view(image, d.plt_got, 8)) return ARTBOX_ELF_INVALID;
    *out = d;
    return ARTBOX_ELF_OK;
}

static int exported(const artbox_elf_symbol *symbol) {
    return symbol->section && (symbol->binding == 1 || symbol->binding == 2 || symbol->binding == 10) &&
           (symbol->visibility == 0 || symbol->visibility == 3);
}

artbox_elf_result artbox_dynamic_lookup_version(const artbox_dynamic *d, const char *name,
    const char *version, artbox_elf_symbol *out) {
    uint32_t index, hash, steps = 0;
    artbox_elf_symbol symbol, weak = {0};
    if (!d || !name || !out || !d->symbols || !d->strings || !d->symbol_count) return ARTBOX_ELF_INVALID;
    unsigned wanted = 1;
    if (version) {
        for (unsigned i = 0; i < d->version_count; ++i)
            if (!d->versions[i].file && !strcmp(d->versions[i].name, version)) wanted = d->versions[i].index;
    }
    if (d->gnu_bucket_count) {
        uint64_t mask, word;
        hash = gnu_hash(name); mask = bloom_bits(hash, d->gnu_shift);
        word = u64(d->gnu_bloom + (size_t)((hash / 64) % d->gnu_bloom_count) * 8);
        if ((word & mask) != mask) return ARTBOX_ELF_NOT_FOUND;
        index = u32(d->gnu_buckets + (size_t)(hash % d->gnu_bucket_count) * 4);
    } else if (d->sysv_bucket_count) {
        hash = sysv_hash(name);
        index = u32(d->sysv_buckets + (size_t)(hash % d->sysv_bucket_count) * 4);
    } else return ARTBOX_ELF_INVALID;
    while (index) {
        uint32_t chain = 0;
        if (++steps >= d->symbol_count || index >= d->symbol_count) return ARTBOX_ELF_INVALID;
        if (d->gnu_bucket_count) {
            if (index < d->gnu_symbol_offset) return ARTBOX_ELF_INVALID;
            chain = u32(d->gnu_chains + (size_t)(index - d->gnu_symbol_offset) * 4);
        }
        unsigned raw_version = d->versym ? u16(d->versym + (size_t)index * 2) : 1u;
        int version_match = !d->versym || (version ? wanted == (raw_version & 0x7fff) : !(raw_version & 0x8000));
        if (version_match && (!d->gnu_bucket_count || (chain & ~1u) == (hash & ~1u)) &&
            artbox_dynamic_symbol(d, index, &symbol) == ARTBOX_ELF_OK && exported(&symbol) &&
            strcmp(symbol.name, name) == 0) {
            if (symbol.binding != 2) { *out = symbol; return ARTBOX_ELF_OK; }
            weak = symbol;
        }
        if (d->gnu_bucket_count) { if (chain & 1) break; ++index; }
        else index = u32(d->sysv_chains + (size_t)index * 4);
    }
    if (weak.name) { *out = weak; return ARTBOX_ELF_OK; }
    return ARTBOX_ELF_NOT_FOUND;
}

artbox_elf_result artbox_dynamic_lookup(const artbox_dynamic *d, const char *name, artbox_elf_symbol *out) {
    return artbox_dynamic_lookup_version(d, name, NULL, out);
}
