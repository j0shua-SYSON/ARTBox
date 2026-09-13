#include "artbox/elf.h"
#include <string.h>

static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const unsigned char *p) {
    return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32;
}
static int in_file(size_t size, uint64_t offset, uint64_t length) {
    return offset <= size && length <= (uint64_t)size - offset;
}

artbox_elf_result artbox_elf_validate(const void *input, size_t size, artbox_elf *out) {
    const unsigned char *data = input;
    artbox_elf result;
    uint64_t phoff;
    uint16_t count;
    unsigned i, j;
    int executable_entry = 0;
    if (data == NULL || out == NULL || size < 64 || memcmp(data, "\177ELF", 4) != 0)
        return ARTBOX_ELF_INVALID;
    if (data[4] != 2 || data[5] != 1 || u16(data + 18) != 183 || u16(data + 16) != 2)
        return ARTBOX_ELF_UNSUPPORTED;
    if (data[6] != 1 || u32(data + 20) != 1 || u16(data + 52) != 64 || u16(data + 54) != 56)
        return ARTBOX_ELF_INVALID;
    if ((data[7] != 0 && data[7] != 3) || u32(data + 48) != 0)
        return ARTBOX_ELF_UNSUPPORTED;
    count = u16(data + 56);
    phoff = u64(data + 32);
    if (count == 0 || count > ARTBOX_ELF_MAX_SEGMENTS || phoff < 64 ||
        !in_file(size, phoff, (uint64_t)count * 56))
        return ARTBOX_ELF_INVALID;
    memset(&result, 0, sizeof(result));
    result.data = data; result.size = size; result.entry = u64(data + 24);
    if (result.entry & 3) return ARTBOX_ELF_INVALID;
    for (i = 0; i < count; ++i) {
        const unsigned char *p = data + (size_t)phoff + i * 56;
        uint32_t type = u32(p);
        artbox_elf_segment segment;
        if (type == 2 || type == 3 || type == 7) return ARTBOX_ELF_UNSUPPORTED;
        if (type == 0x6474e551 && (u32(p + 4) & 1)) return ARTBOX_ELF_UNSUPPORTED;
        if (type != 1) continue;
        segment.flags = u32(p + 4);
        segment.file_offset = u64(p + 8); segment.virtual_address = u64(p + 16);
        segment.file_size = u64(p + 32); segment.memory_size = u64(p + 40);
        segment.alignment = u64(p + 48);
        if ((segment.flags & ~7u) || (segment.flags & 3) == 3)
            return ARTBOX_ELF_UNSUPPORTED;
        if (segment.file_size > segment.memory_size ||
            !in_file(size, segment.file_offset, segment.file_size) ||
            segment.memory_size > UINT64_MAX - segment.virtual_address)
            return ARTBOX_ELF_INVALID;
        if (segment.alignment > 1 &&
            ((segment.alignment & (segment.alignment - 1)) != 0 ||
             ((segment.virtual_address - segment.file_offset) & (segment.alignment - 1)) != 0))
            return ARTBOX_ELF_INVALID;
        if ((segment.flags & 1) && segment.file_size != segment.memory_size)
            return ARTBOX_ELF_UNSUPPORTED;
        for (j = 0; j < result.segment_count; ++j) {
            const artbox_elf_segment *other = &result.segments[j];
            if (segment.memory_size && other->memory_size &&
                segment.virtual_address < other->virtual_address + other->memory_size &&
                other->virtual_address < segment.virtual_address + segment.memory_size)
                return ARTBOX_ELF_INVALID;
        }
        if ((segment.flags & 5) == 5 && result.entry >= segment.virtual_address &&
            result.entry - segment.virtual_address < segment.file_size)
            executable_entry = 1;
        result.segments[result.segment_count++] = segment;
    }
    if (!executable_entry || result.segment_count == 0) return ARTBOX_ELF_INVALID;
    *out = result;
    return ARTBOX_ELF_OK;
}

artbox_elf_result artbox_elf_find_section(const artbox_elf *image, const char *name,
                                        artbox_elf_section *out) {
    const unsigned char *data, *strings;
    uint64_t shoff, string_offset, string_size;
    uint16_t count, string_index;
    size_t name_size;
    unsigned i;
    int found = 0;
    artbox_elf_section result;
    if (!image || !name || !out || !image->data || image->size < 64)
        return ARTBOX_ELF_INVALID;
    data = image->data;
    shoff = u64(data + 40); count = u16(data + 60); string_index = u16(data + 62);
    if (count == 0) return ARTBOX_ELF_UNSUPPORTED;
    if (count > 256 || u16(data + 58) != 64 || string_index == 0 || string_index >= count ||
        !in_file(image->size, shoff, (uint64_t)count * 64)) return ARTBOX_ELF_INVALID;
    strings = data + (size_t)shoff + string_index * 64;
    if (u32(strings + 4) != 3) return ARTBOX_ELF_INVALID;
    string_offset = u64(strings + 24); string_size = u64(strings + 32);
    if (!in_file(image->size, string_offset, string_size)) return ARTBOX_ELF_INVALID;
    name_size = strlen(name);
    for (i = 1; i < count; ++i) {
        const unsigned char *section = data + (size_t)shoff + i * 64;
        uint32_t index = u32(section);
        const unsigned char *label;
        uint64_t remaining;
        if (index >= string_size) return ARTBOX_ELF_INVALID;
        label = data + (size_t)string_offset + index;
        remaining = string_size - index;
        if (memchr(label, 0, (size_t)remaining) == NULL) return ARTBOX_ELF_INVALID;
        if (name_size >= remaining || memcmp(label, name, name_size) || label[name_size] != 0)
            continue;
        if (found) return ARTBOX_ELF_INVALID;
        result.type = u32(section + 4); result.flags = u64(section + 8);
        result.address = u64(section + 16); result.offset = u64(section + 24);
        result.size = u64(section + 32);
        if (result.type == 8 || !in_file(image->size, result.offset, result.size) ||
            result.size > UINT64_MAX - result.address) return ARTBOX_ELF_INVALID;
        found = 1;
    }
    if (!found) return ARTBOX_ELF_NOT_FOUND;
    *out = result;
    return ARTBOX_ELF_OK;
}

const char *artbox_elf_result_string(artbox_elf_result result) {
    switch (result) {
    case ARTBOX_ELF_OK: return "valid supported ELF";
    case ARTBOX_ELF_INVALID: return "invalid ELF bounds or structure";
    case ARTBOX_ELF_UNSUPPORTED: return "unsupported ELF feature";
    case ARTBOX_ELF_NOT_FOUND: return "required ELF section not found";
    default: return "unknown ELF result";
    }
}
