#include "artbox/dynamic.h"
#include "artbox/relocation.h"
#include "artbox/guest.h"
#include "artbox/native_call.h"
#include "artbox/native_memory.h"
#include "artbox/native_syscall.h"
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static _Thread_local int guest_errno;
static int* errno_pointer(void) { return &guest_errno; }
static unsigned writes;
static const char expected[] = "hello from dynamic Bionic\n";

static uint64_t now(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) exit(2);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static artbox_elf_result resolve(void* context, const artbox_dynamic* dynamic, uint32_t index, uint64_t* address) {
    artbox_elf_symbol symbol;
    (void)context;
    if (artbox_dynamic_symbol(dynamic, index, &symbol) != ARTBOX_ELF_OK) return ARTBOX_ELF_INVALID;
    if (!strcmp(symbol.name, "artbox_stub_artbox_bionic_syscall")) {
        int64_t (*entry)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = artbox_bionic_syscall;
        _Static_assert(sizeof(entry) == sizeof(*address), "AArch64 function address size");
        memcpy(address, &entry, sizeof(entry));
        return ARTBOX_ELF_OK;
    }
    if (!strcmp(symbol.name, "artbox_stub___errno")) {
        int* (*entry)(void) = errno_pointer;
        _Static_assert(sizeof(entry) == sizeof(*address), "AArch64 function address size");
        memcpy(address, &entry, sizeof(entry));
        return ARTBOX_ELF_OK;
    }
    return ARTBOX_ELF_NOT_FOUND;
}

static const void* lookup(const artbox_dynamic* dynamic, const unsigned char* base, const char* name, unsigned type) {
    artbox_elf_symbol symbol;
    if (artbox_dynamic_lookup(dynamic, name, &symbol) != ARTBOX_ELF_OK || symbol.type != type ||
        symbol.value >= dynamic->image->segments[0].file_size ||
        symbol.size > dynamic->image->segments[0].file_size - symbol.value || (type == 2 && symbol.value % 4)) return NULL;
    return base + symbol.value;
}

static int64_t output(void* context, int fd, const void* bytes, size_t size) {
    (void)context;
    if (fd != 1 || size != sizeof(expected) - 1 || memcmp(bytes, expected, size)) return -5;
    ++writes;
    return (int64_t)size;
}

static int64_t dispatch(void* context, uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                        uint64_t a3, uint64_t a4, uint64_t a5) {
    return artbox_guest_call(context, n, a0, a1, a2, a3, a4, a5);
}

static void exit_entry(const void* entry, artbox_guest* guest, artbox_dispatch_fn ignored, void* stack) {
    (void)guest; (void)ignored; (void)stack;
    artbox_call7(entry, 93, 0, 0, 0, 0, 0, 0);
    abort();
}

int main(int argc, char** argv) {
    FILE* input;
    long length;
    unsigned char* original;
    void* library;
    unsigned char *rx, *rw;
    artbox_elf elf;
    artbox_dynamic dynamic;
    artbox_relocation_memory destination;
    artbox_relocation_stats stats = {0};
    artbox_memory_ops memory = artbox_native_memory();
    artbox_guest* guest;
    const void *generic, *probe, *message;
    uint64_t started, loaded, relocated, finished;
    if (argc != 3) return 2;
    input = fopen(argv[2], "rb");
    if (!input || fseek(input, 0, SEEK_END) || (length = ftell(input)) < 0 || length > 64 * 1024 * 1024 || fseek(input, 0, SEEK_SET)) return 2;
    original = malloc((size_t)length);
    if (!original || fread(original, 1, (size_t)length, input) != (size_t)length || fclose(input)) return 2;
    if (artbox_elf_open(original, (size_t)length, &elf) != ARTBOX_ELF_OK || elf.type != 3 || elf.segment_count != 2 ||
        elf.segments[0].virtual_address != 0 || elf.segments[0].flags != 5 || elf.segments[1].flags != 6 ||
        artbox_dynamic_open(&elf, &dynamic) != ARTBOX_ELF_OK || dynamic.needed_count || dynamic.init || dynamic.fini ||
        dynamic.preinit_array.size || dynamic.init_array.size != 8 || dynamic.fini_array.size) return 1;
    started = now();
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    rx = dlsym(library, "artbox_dynamic_rx");
    rw = dlsym(library, "artbox_dynamic_rw");
    if (!rx || !rw || (uintptr_t)rx % 16384 || (uintptr_t)rw != (uintptr_t)rx + elf.segments[1].virtual_address ||
        memcmp(rx, original, (size_t)elf.segments[0].file_size) ||
        memcmp(rw, original + elf.segments[1].file_offset, (size_t)elf.segments[1].file_size)) return 1;
    for (uint64_t i = elf.segments[1].file_size; i < elf.segments[1].memory_size; ++i) if (rw[i]) return 1;
    loaded = now();
    destination = (artbox_relocation_memory){1, rw, (size_t)elf.segments[1].memory_size};
    if (artbox_relocate(&dynamic, (uintptr_t)rx, &destination, 1, resolve, NULL, &stats) != ARTBOX_ELF_OK ||
        !stats.plt_count || !stats.relr_count) return 1;
    generic = lookup(&dynamic, rx, "artbox_stub_syscall", 2);
    probe = lookup(&dynamic, rx, "artbox_dynamic_probe", 2);
    message = lookup(&dynamic, rx, "artbox_dynamic_message", 1);
    if (!generic || !probe || !message) return 1;
    {
        uint64_t constructor;
        if (dynamic.init_array.address < elf.segments[1].virtual_address ||
            dynamic.init_array.address + 8 > elf.segments[1].virtual_address + elf.segments[1].memory_size) return 1;
        memcpy(&constructor, rx + dynamic.init_array.address, 8);
        if (constructor < (uintptr_t)rx || constructor >= (uintptr_t)rx + elf.segments[0].file_size || constructor % 4) return 1;
        artbox_call7((const void*)(uintptr_t)constructor, 0, 0, 0, 0, 0, 0, 0);
    }
    relocated = now();
    guest = artbox_guest_create(&memory, output, NULL, rx, (size_t)elf.segments[0].file_size);
    if (!guest) return 1;
    const artbox_syscall_binding binding = {dispatch, guest};
    const artbox_syscall_binding* previous = artbox_native_syscall_swap(&binding);
    for (unsigned i = 0; i < 100; ++i) {
        uint64_t mapped;
        if (artbox_call7(probe, 0, 0, 0, 0, 0, 0, 0) != UINT64_C(0x123456789abcdef0)) return 1;
        errno = EDOM;
        if (artbox_call7(generic, 9999, 0, 0, 0, 0, 0, 0) != UINT64_MAX || guest_errno != 38 || errno != EDOM) return 1;
        if (artbox_call7(generic, 64, 99, 0, 0, 0, 0, 0) != UINT64_MAX || guest_errno != 9 || errno != EDOM) return 1;
        if (artbox_call7(generic, 64, 1, (uintptr_t)message, sizeof(expected) - 1, 0, 0, 0) != sizeof(expected) - 1) return 1;
        mapped = artbox_call7(generic, 222, 0, memory.page_size, 3, 0x22, UINT64_MAX, 0);
        if (!mapped || mapped >= UINT64_MAX - 4095) return 1;
        memcpy((void*)(uintptr_t)mapped, expected, sizeof(expected));
        if (artbox_call7(generic, 226, mapped, memory.page_size, 1, 0, 0, 0)) return 1;
        if (artbox_call7(generic, 64, 1, mapped, sizeof(expected) - 1, 0, 0, 0) != sizeof(expected) - 1) return 1;
        if (artbox_call7(generic, 215, mapped, memory.page_size, 0, 0, 0, 0) ||
            artbox_guest_execute(guest, generic, exit_entry) != 0) return 1;
    }
    artbox_native_syscall_swap(previous);
    finished = now();
    if (writes != 200 || memcmp(rx, original, (size_t)elf.segments[0].file_size) ||
        artbox_guest_syscall_count(guest, 64) != 300 || artbox_guest_syscall_count(guest, 93) != 100 ||
        artbox_guest_syscall_count(guest, 222) != 100 || artbox_guest_syscall_count(guest, 226) != 100 ||
        artbox_guest_syscall_count(guest, 215) != 100 || artbox_guest_destroy(guest) || dlclose(library)) return 1;
    free(original);
    printf("{\"iterations\":100,\"writes\":200,\"exit_status\":0,\"constructor_runs\":1,"
           "\"rela\":%zu,\"plt\":%zu,\"relr\":%zu,\"dlopen_and_validate_ns\":%" PRIu64 ","
           "\"relocate_and_construct_ns\":%" PRIu64 ",\"iterations_ns\":%" PRIu64 "}\n",
           stats.rela_count, stats.plt_count, stats.relr_count, loaded - started, relocated - loaded, finished - relocated);
    return 0;
}
