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

int64_t artbox_file_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                           uint64_t a3, uint64_t a4, uint64_t a5) {
    return syscall((long)n, a0, a1, a2, a3, a4, a5);
}
int *artbox_file_errno(void) { return &errno; }
int64_t artbox_signal_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                             uint64_t a3, uint64_t a4, uint64_t a5) {
    long result = syscall((long)n, a0, a1, a2, a3, a4, a5);
    return result == -1 ? -errno : result;
}
int64_t artbox_stub_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                           uint64_t a3, uint64_t a4, uint64_t a5) {
    return syscall((long)n, a0, a1, a2, a3, a4, a5);
}
int *artbox_stub___errno(void) { return &errno; }

int artbox_allocator_pressure_check(void) {
    // Retain 24 MiB in one size class: a 16 MiB primary region must exhaust
    // safely and continue through Scudo's existing larger-class/secondary path.
    enum { COUNT = 384, SIZE = 65536 };
    unsigned char *blocks[COUNT] = {0};
    int result = -1;
    for (unsigned i = 0; i < COUNT; ++i) {
        blocks[i] = malloc(SIZE);
        if (!blocks[i] || (uintptr_t)blocks[i] % 16 || malloc_usable_size(blocks[i]) < SIZE) goto done;
        memset(blocks[i], (unsigned char)(i + 1), SIZE);
    }
    result = -2;
    for (unsigned i = 0; i < COUNT; ++i)
        for (unsigned j = 0; j < SIZE; ++j)
            if (blocks[i][j] != (unsigned char)(i + 1)) goto done;
    result = -3;
    unsigned char *grown = realloc(blocks[0], SIZE * 2);
    if (!grown) goto done;
    blocks[0] = grown;
    for (unsigned j = 0; j < SIZE; ++j) if (grown[j] != 1) goto done;
    result = 3;
done:
    for (unsigned i = 0; i < COUNT; ++i) free(blocks[i]);
    return result;
}

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
