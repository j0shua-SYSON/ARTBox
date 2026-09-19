#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The exact original NDK DSO is loaded by the native Linux linker. This driver
 * supplies aligned storage below 4 GiB; it does not construct ART Objects. */
int main(int argc, char **argv) {
    if ((argc != 3 && argc != 4) || (strcmp(argv[2], "plain") && strcmp(argv[2], "poisoned"))) return 2;
    int storage = argc == 4;
    if (storage && strcmp(argv[3], "storage")) return 2;
    int expected_cases = storage ? 27 : 19;
    int poison = !strcmp(argv[2], "poisoned");
    long page = sysconf(_SC_PAGESIZE);
    if (page < 4096 || page > 65536) return 2;
    size_t span = (size_t)page * 4;
    unsigned char *base = MAP_FAILED;
    for (uintptr_t hint = UINT64_C(0x40000000); hint < UINT64_C(0xf0000000); hint += UINT64_C(0x4000000)) {
        void *candidate = mmap((void *)hint, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (candidate == MAP_FAILED) continue;
        if ((uintptr_t)candidate >= (uintptr_t)page && (uintptr_t)candidate < UINT64_C(0x100000000) - span) {
            base = candidate;
            break;
        }
        if (munmap(candidate, span)) return 1;
    }
    if (base == MAP_FAILED || mprotect(base + page, (size_t)page * 2, PROT_READ | PROT_WRITE)) return 1;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    void *symbol = dlsym(library, storage ? "artbox_art_storage_check" : "artbox_art_reference_check");
    int (*check)(void *, void *, uint32_t, uint32_t, uint32_t *, void *);
    _Static_assert(sizeof(check) == sizeof(symbol), "native function address width");
    memcpy(&check, &symbol, sizeof(check));
    if (!check) return 1;
    void *first = base + page, *second = base + 2 * page;
    uint32_t observations[4] = {0}, first_bits = (uint32_t)(uintptr_t)first, second_bits = (uint32_t)(uintptr_t)second;
    int result = check(first, second, first_bits, second_bits, observations, base + page + 16);
    uint32_t fourth = storage ? ((second_bits >> 3) | UINT32_C(0xc0000000)) : second_bits;
    if (result != expected_cases || *(uint64_t *)first != UINT64_C(0x123456789abcdef0) ||
        *(uint64_t *)second != UINT64_C(0xfedcba9876543210) || observations[0] != (uint32_t)poison ||
        observations[1] != (poison ? 0u-first_bits : first_bits) ||
        observations[2] != (poison ? 0u-second_bits : second_bits) || observations[3] != fourth) {
        fprintf(stderr, "Original ART reference case: %d\n", result);
        return 1;
    }
    uintptr_t address = (uintptr_t)base;
    if (dlclose(library) || munmap(base, span)) return 1;
    printf("{\"cases\":%d,\"native_base\":%" PRIuPTR ",\"encoding\":\"absolute\",\"cleanup\":true,"
           "\"observations\":[%u,%u,%u,%u]}\n", expected_cases, address,
           observations[0], observations[1], observations[2], observations[3]);
    return 0;
}
