// Original NDK client, MIT. Links dynamically to the selected real Bionic.
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) return -__LINE__; ++cases; } while (0)
extern void artbox_bootstrap_note_allocation(const void* p);
int64_t artbox_futex_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5) {
    return syscall((long)n, a0, a1, a2, a3, a4, a5);
}
int *artbox_futex_errno(void) { return &errno; }

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
        artbox_bootstrap_note_allocation(p);
        CHECK(malloc_usable_size(p) >= size);
        memset(p, 0xa5, size);
        unsigned char* grown = realloc(p, size * 2);
        CHECK(grown != NULL);
        for (size_t i = 0; i < size; ++i) if (grown[i] != 0xa5) return -__LINE__;
        CHECK(1);
        free(grown);
        p = calloc(size, 1);
        CHECK(p != NULL);
        artbox_bootstrap_note_allocation(p);
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
    unsigned char data[32];
    memset(data, 0x5a, sizeof(data));
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 3);
    CHECK(read(fd, data + 1, 16) == 16);
    CHECK(data[0] == 0x5a && data[17] == 0x5a);
    struct stat st;
    CHECK(fstat(fd, &st) == 0 && S_ISCHR(st.st_mode));
    CHECK(lseek(fd, 123, SEEK_SET) == 0);
    CHECK(close(fd) == 0);
    CHECK(close(fd) == -1 && errno == EBADF);
    fd = open("/dev/zero", O_RDONLY);
    CHECK(fd >= 3 && read(fd, data, sizeof(data)) == sizeof(data));
    unsigned total = 0;
    for (unsigned i = 0; i < sizeof(data); ++i) total |= data[i];
    CHECK(total == 0);
    CHECK(close(fd) == 0);
    fd = open("/dev/null", O_RDWR);
    CHECK(fd >= 3 && write(fd, data, sizeof(data)) == sizeof(data) && read(fd, data, sizeof(data)) == 0);
    CHECK(close(fd) == 0);
    return cases;
}
