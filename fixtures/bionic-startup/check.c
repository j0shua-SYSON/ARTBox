// Original NDK client, MIT. Links dynamically to the selected real Bionic.
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) return -__LINE__; ++cases; } while (0)

int artbox_startup_check(void) {
    int cases = 0;
    CHECK(getpid() == 10000 && gettid() == 10000);
    CHECK(pthread_self() != 0);
    CHECK(getpagesize() >= 4096);
    errno = EDOM;
    CHECK(syscall(-1L) == -1 && errno == ENOSYS);
    for (size_t size = 1; size <= 1024 * 1024; size *= 2) {
        unsigned char* p = malloc(size);
        CHECK(p != NULL && (uintptr_t)p % 16 == 0);
        CHECK(malloc_usable_size(p) >= size);
        memset(p, 0xa5, size);
        unsigned char* grown = realloc(p, size * 2);
        CHECK(grown != NULL);
        for (size_t i = 0; i < size; ++i) if (grown[i] != 0xa5) return -__LINE__;
        CHECK(1);
        free(grown);
        p = calloc(size, 1);
        CHECK(p != NULL);
        for (size_t i = 0; i < size; ++i) if (p[i] != 0) return -__LINE__;
        CHECK(1);
        free(p);
    }
    void* aligned = NULL;
    CHECK(posix_memalign(&aligned, 16384, 32768) == 0 && (uintptr_t)aligned % 16384 == 0);
    memset(aligned, 0x5a, 32768);
    free(aligned);
    CHECK(posix_memalign(&aligned, 3, 1) == EINVAL);
    errno = 0;
    volatile size_t huge = SIZE_MAX;
    CHECK(calloc(huge, 2) == NULL && errno == ENOMEM);
    char* text = strdup("Bionic allocator ready");
    CHECK(text != NULL && strcmp(text, "Bionic allocator ready") == 0);
    free(text);
    return cases;
}
