#include "artbox/guest.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define MAP_CAPACITY 16
#define STACK_SIZE (128u * 1024u)
#define MEMORY_LIMIT (64u * 1024u * 1024u)
typedef struct mapping { void *base; size_t size; unsigned protection; } mapping;
struct artbox_guest {
    artbox_memory_ops memory;
    artbox_output_fn output;
    void *output_context;
    const void *image;
    size_t image_size;
    void *stack;
    mapping mappings[MAP_CAPACITY];
    size_t mapped_bytes;
    jmp_buf escape;
    int active, exit_status;
    uint64_t calls[5];
};

static int counter_index(uint64_t nr) {
    switch (nr) { case 64: return 0; case 93: return 1; case 222: return 2;
                  case 226: return 3; case 215: return 4; default: return -1; }
}
static int contains(const void *base, size_t size, uint64_t pointer, uint64_t length) {
    uint64_t start = (uintptr_t)base;
    return pointer >= start && pointer - start <= size && length <= size - (pointer - start);
}
static int readable(const artbox_guest *guest, uint64_t pointer, uint64_t length) {
    unsigned i;
    if (contains(guest->image, guest->image_size, pointer, length) ||
        contains(guest->stack, STACK_SIZE, pointer, length)) return 1;
    for (i = 0; i < MAP_CAPACITY; ++i)
        if (guest->mappings[i].base && (guest->mappings[i].protection & 1) &&
            contains(guest->mappings[i].base, guest->mappings[i].size, pointer, length)) return 1;
    return 0;
}
static int protection_error(uint64_t prot) {
    if (prot & ~UINT64_C(7)) return -22;
    if (prot & 4) return -1;
    if (prot == 2) return -95; /* Write-only native-page semantics vary. */
    return 0;
}
static size_t rounded(const artbox_guest *guest, uint64_t length) {
    uint64_t mask = guest->memory.page_size - 1;
    if (!length || length > SIZE_MAX - mask) return 0;
    return (size_t)((length + mask) & ~mask);
}

artbox_guest *artbox_guest_create(const artbox_memory_ops *memory, artbox_output_fn output,
                                void *context, const void *image, size_t image_size) {
    artbox_guest *guest;
    if (!memory || !memory->map || !memory->protect || !memory->unmap || !output ||
        !image || !image_size || memory->page_size < 4096 || memory->page_size > 65536 ||
        (memory->page_size & (memory->page_size - 1))) return NULL;
    guest = calloc(1, sizeof(*guest));
    if (!guest) return NULL;
    guest->memory = *memory; guest->output = output; guest->output_context = context;
    guest->image = image; guest->image_size = image_size;
    if (memory->map(STACK_SIZE, 3, &guest->stack) != 0 || !guest->stack) {
        free(guest); return NULL;
    }
    return guest;
}

int artbox_guest_destroy(artbox_guest *guest) {
    unsigned i;
    int result = 0;
    if (!guest || guest->active) return -22;
    for (i = 0; i < MAP_CAPACITY; ++i)
        if (guest->mappings[i].base &&
            guest->memory.unmap(guest->mappings[i].base, guest->mappings[i].size) != 0) result = -5;
    if (guest->memory.unmap(guest->stack, STACK_SIZE) != 0) result = -5;
    free(guest);
    return result;
}

int64_t artbox_guest_call(artbox_guest *guest, uint64_t nr, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    int index, error;
    unsigned i;
    size_t size;
    if (!guest) return -22;
    index = counter_index(nr);
    if (index < 0) return -38;
    ++guest->calls[index];
    switch (nr) {
    case 64:
        if (a0 != 1 && a0 != 2) return -9;
        if (!a2) return 0;
        if (a2 > INT64_MAX || !readable(guest, a1, a2)) return -14;
        {
            int64_t result = guest->output(guest->output_context, (int)a0, (const void *)(uintptr_t)a1, (size_t)a2);
            return result > (int64_t)a2 ? -5 : result;
        }
    case 93:
        if (!guest->active) return -1;
        guest->exit_status = (int)(a0 & 255);
        longjmp(guest->escape, 1);
    case 222:
        size = rounded(guest, a1);
        if (!size) return -22;
        error = protection_error(a2); if (error) return error;
        if (a0 || a3 != 0x22 || a4 != UINT64_MAX || a5) return -95;
        if (size > MEMORY_LIMIT - guest->mapped_bytes) return -12;
        for (i = 0; i < MAP_CAPACITY; ++i) if (!guest->mappings[i].base) {
            void *address = NULL;
            error = guest->memory.map(size, (unsigned)a2, &address);
            if (error) return error;
            if (!address) return -12;
            guest->mappings[i].base = address; guest->mappings[i].size = size;
            guest->mappings[i].protection = (unsigned)a2; guest->mapped_bytes += size;
            return (int64_t)(uintptr_t)address;
        }
        return -12;
    case 226:
    case 215:
        if (a0 & (guest->memory.page_size - 1)) return -22;
        size = rounded(guest, a1);
        if (!size) return -22;
        if (nr == 226) { error = protection_error(a2); if (error) return error; }
        for (i = 0; i < MAP_CAPACITY; ++i) if ((uintptr_t)guest->mappings[i].base == a0 && a0) {
            mapping *map = &guest->mappings[i];
            if (size != map->size) return -95;
            if (nr == 226) {
                error = guest->memory.protect(map->base, map->size, (unsigned)a2);
                if (!error) map->protection = (unsigned)a2;
            } else {
                error = guest->memory.unmap(map->base, map->size);
                if (!error) { guest->mapped_bytes -= map->size; memset(map, 0, sizeof(*map)); }
            }
            return error;
        }
        return -95; /* Ownership/whole-mapping subset; Linux allows holes. */
    default: return -38;
    }
}

int artbox_guest_execute(artbox_guest *guest, const void *entry, artbox_enter_fn enter) {
    uintptr_t *stack;
    char *argument;
    if (!guest || !enter || guest->active || !contains(guest->image, guest->image_size, (uintptr_t)entry, 1))
        return ARTBOX_GUEST_INVALID;
    stack = (uintptr_t *)((unsigned char *)guest->stack + STACK_SIZE - 128);
    argument = (char *)guest->stack + STACK_SIZE - 32;
    memcpy(argument, "artbox-hello", sizeof("artbox-hello"));
    memset(stack, 0, 12 * sizeof(uintptr_t));
    stack[0] = 1; stack[1] = (uintptr_t)argument;
    stack[4] = 6; stack[5] = guest->memory.page_size; /* AT_PAGESZ */
    stack[6] = 9; stack[7] = (uintptr_t)entry; /* AT_ENTRY; AT_NULL follows */
    if (setjmp(guest->escape) == 0) {
        guest->active = 1;
        enter(entry, guest, artbox_guest_call, stack);
        guest->active = 0;
        return ARTBOX_GUEST_RETURNED;
    }
    guest->active = 0;
    return guest->exit_status;
}

uint64_t artbox_guest_syscall_count(const artbox_guest *guest, uint64_t number) {
    int index = counter_index(number);
    return guest && index >= 0 ? guest->calls[index] : 0;
}
