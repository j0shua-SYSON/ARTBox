/* Original host address-space probe. This does not execute ART or DEX. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static size_t native_page_size(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}
static void *reserve_hint(uintptr_t hint, size_t length) {
    return VirtualAlloc((void *)hint, length, MEM_RESERVE, PAGE_NOACCESS);
}
static int make_writable(void *address, size_t length) {
    return VirtualAlloc(address, length, MEM_COMMIT, PAGE_READWRITE) == address;
}
static int release(void *address, size_t length) {
    (void)length;
    return VirtualFree(address, 0, MEM_RELEASE) != 0;
}
#else
#include <sys/mman.h>
#include <unistd.h>
static size_t native_page_size(void) {
    long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? (size_t)value : 0;
}
static void *reserve_hint(uintptr_t hint, size_t length) {
    void *result = mmap((void *)hint, length, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return result == MAP_FAILED ? NULL : result;
}
static int make_writable(void *address, size_t length) {
    return mprotect(address, length, PROT_READ | PROT_WRITE) == 0;
}
static int release(void *address, size_t length) {
    return munmap(address, length) == 0;
}
#endif

static void *reserve_low(size_t length, unsigned *attempts, int *cleanup_failed) {
    /* Hints only: never replace an existing image, guard, heap or stack. */
    for (uint64_t hint = UINT64_C(0x40000000);
         hint + length <= UINT64_C(0x100000000); hint += UINT64_C(0x04000000)) {
        ++*attempts;
        void *result = reserve_hint((uintptr_t)hint, length);
        if (result == NULL) continue;
        uint64_t address = (uintptr_t)result;
        if (address >= UINT64_C(0x10000) && address < UINT64_C(0x100000000) &&
            length <= UINT64_C(0x100000000) - address) return result;
        if (!release(result, length)) { *cleanup_failed = 1; return NULL; }
    }
    return NULL;
}

static int check_pages(void *base, size_t length, size_t page, unsigned salt) {
    unsigned char *first = (unsigned char *)base;
    unsigned char *last = first + length - page;
    if (!make_writable(first, page) || !make_writable(last, page)) return 0;
    for (size_t i = 0; i < page; ++i) if (first[i] != 0 || last[i] != 0) return 0;
    memset(first, (int)salt, page);
    memset(last, (int)(salt + 1), page);
    uint32_t first_ref = (uint32_t)(uintptr_t)first;
    uint32_t last_ref = (uint32_t)(uintptr_t)last;
    if ((uintptr_t)first_ref != (uintptr_t)first || (uintptr_t)last_ref != (uintptr_t)last) return 0;
    const unsigned char *first_again = (const unsigned char *)(uintptr_t)first_ref;
    const unsigned char *last_again = (const unsigned char *)(uintptr_t)last_ref;
    for (size_t i = 0; i < page; ++i)
        if (first_again[i] != salt || last_again[i] != salt + 1) return 0;
    return 1;
}

int main(int argc, char **argv) {
    if (sizeof(uintptr_t) < 8) return 77;
    if (argc != 2 || (strcmp(argv[1], "available") && strcmp(argv[1], "blocked"))) return 2;
    int expected = strcmp(argv[1], "available") == 0;
    const size_t length = 64u * 1024u * 1024u;
    size_t page = native_page_size();
    if (!page || page > length / 2 || length % page) return 3;
    unsigned attempts = 0;
    int cleanup_failed = 0;
    void *first = reserve_low(length, &attempts, &cleanup_failed);
    void *second = first ? reserve_low(length, &attempts, &cleanup_failed) : NULL;
    uint64_t a = (uintptr_t)first, b = (uintptr_t)second;
    int available = first != NULL && second != NULL;
    int pages_valid = 1;
    if (available) {
        pages_valid = (a + length <= b || b + length <= a) &&
                      check_pages(first, length, page, 41) && check_pages(second, length, page, 73);
    }
    if (second && !release(second, length)) cleanup_failed = 1;
    if (first && !release(first, length)) cleanup_failed = 1;
    int valid = !cleanup_failed && pages_valid;
    /* A partially available low window is not evidence of complete blocking. */
    int matches = expected ? available : first == NULL;
    printf("{\"low_window_available\":%s,\"first\":%" PRIu64 ",\"second\":%" PRIu64
           ",\"reservation_bytes\":%zu,\"page_size\":%zu,\"attempts\":%u,"
           "\"reference_roundtrip\":%s,\"cleanup_ok\":%s,\"success\":%s}\n",
           available ? "true" : "false", a, b, length, page, attempts,
           available && pages_valid ? "true" : "false", !cleanup_failed ? "true" : "false",
           matches && valid ? "true" : "false");
    return matches && valid ? 0 : 1;
}
