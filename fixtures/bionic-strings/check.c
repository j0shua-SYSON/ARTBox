/* Original test code. The callees are the pinned, unmodified AOSP objects.
 * Both buffers are one writable page surrounded by inaccessible pages. */
#include <stddef.h>
#include <stdint.h>

void* artbox_string_memchr(const void*, int, size_t);
int artbox_string_memcmp(const void*, const void*, size_t);
void* artbox_string_memcpy(void*, const void*, size_t);
void* artbox_string_memmove(void*, const void*, size_t);
void* artbox_string_memrchr(const void*, int, size_t);
void* artbox_string_memset(void*, int, size_t);
char* artbox_string_stpcpy(char*, const char*);
char* artbox_string_strchr(const char*, int);
char* artbox_string_strchrnul(const char*, int);
int artbox_string_strcmp(const char*, const char*);
char* artbox_string_strcpy(char*, const char*);
size_t artbox_string_strlen(const char*);
char* artbox_string_strrchr(const char*, int);
int artbox_string_strncmp(const char*, const char*, size_t);
size_t artbox_string_strnlen(const char*, size_t);

#define CHECK(condition) do { if (!(condition)) return -(int64_t)__LINE__; } while (0)

static void fill(unsigned char* p, size_t n, unsigned char value) {
    for (size_t i = 0; i < n; ++i) p[i] = value;
}

static int equal(const unsigned char* a, const unsigned char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static int filled(const unsigned char* a, size_t n, unsigned char value) {
    for (size_t i = 0; i < n; ++i) if (a[i] != value) return 0;
    return 1;
}

/* 2050 memory + 1026 string + 32832 overlapping move cases. The scalar
 * oracle never calls libc and is compiled with -fno-builtin. */
int64_t artbox_string_check(unsigned char* a, unsigned char* b, size_t page) {
    uint64_t cases = 0;
    CHECK(a && b && page >= 4096);
    for (size_t n = 0; n <= 1024; ++n) {
        for (unsigned edge = 0; edge < 2; ++edge) {
            size_t ai = edge ? page - n : n % 32;
            size_t bi = edge ? page - n : (31 - n % 32);
            unsigned char *p = a + ai, *q = b + bi;
            fill(a, page, 0xa5);
            fill(b, page, 0x5a);
            for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)(1 + i % 251);
            CHECK(artbox_string_memcpy(q, p, n) == q);
            CHECK(equal(p, q, n));
            CHECK(filled(b, bi, 0x5a) && filled(q + n, page - bi - n, 0x5a));
            CHECK(artbox_string_memcmp(p, q, n) == 0);
            CHECK(artbox_string_memchr(p, 0, n) == NULL);
            CHECK(artbox_string_memrchr(p, 0, n) == NULL);
            CHECK(artbox_string_memchr(p, 0x101, n) == (n ? p : NULL));
            CHECK(artbox_string_memrchr(p, 1, n) == (n ? p + (n - 1) / 251 * 251 : NULL));
            if (n) {
                q[n - 1] = 0;
                CHECK(artbox_string_memcmp(p, q, n) > 0);
                CHECK(artbox_string_memcmp(q, p, n) < 0);
            }
            CHECK(artbox_string_memset(q, 0x100, n) == q);
            CHECK(filled(q, n, 0));
            CHECK(filled(b, bi, 0x5a) && filled(q + n, page - bi - n, 0x5a));
            CHECK(artbox_string_memset(q, 0x180, n) == q);
            CHECK(filled(q, n, 0x80));
            CHECK(filled(b, bi, 0x5a) && filled(q + n, page - bi - n, 0x5a));
            ++cases;
        }
    }
    for (size_t n = 0; n <= 512; ++n) {
        for (unsigned edge = 0; edge < 2; ++edge) {
            size_t ai = edge ? page - n - 1 : n % 32;
            size_t bi = edge ? page - n - 1 : 31 - n % 32;
            char *p = (char*)a + ai, *q = (char*)b + bi;
            fill(a, page, 0xa5);
            fill(b, page, 0x5a);
            for (size_t i = 0; i < n; ++i) p[i] = (char)(1 + i % 251);
            p[n] = 0;
            CHECK(artbox_string_strlen(p) == n);
            CHECK(artbox_string_strnlen(p, n + 1) == n);
            CHECK(artbox_string_strnlen(p, n / 2) == n / 2);
            CHECK(artbox_string_strchr(p, 0) == p + n);
            CHECK(artbox_string_strchrnul(p, 0xff) == p + n);
            CHECK(artbox_string_strrchr(p, 0) == p + n);
            CHECK(artbox_string_strchr(p, 0xff) == NULL);
            CHECK(artbox_string_strrchr(p, 0xff) == NULL);
            CHECK(artbox_string_strchr(p, 0x101) == (n ? p : NULL));
            CHECK(artbox_string_strchrnul(p, 1) == (n ? p : p + n));
            CHECK(artbox_string_strrchr(p, 1) == (n ? p + (n - 1) / 251 * 251 : NULL));
            CHECK(artbox_string_strcpy(q, p) == q);
            CHECK(equal((unsigned char*)p, (unsigned char*)q, n + 1));
            CHECK(filled(b, bi, 0x5a) && filled((unsigned char*)q + n + 1, page - bi - n - 1, 0x5a));
            CHECK(artbox_string_strcmp(p, q) == 0);
            CHECK(artbox_string_strncmp(p, q, n + 1) == 0);
            if (n) {
                q[n - 1] = 0;
                CHECK(artbox_string_strcmp(p, q) > 0);
                CHECK(artbox_string_strcmp(q, p) < 0);
                CHECK(artbox_string_strncmp(p, q, n - 1) == 0);
                CHECK(artbox_string_strncmp(q, p, n) < 0);
            }
            fill(b, page, 0x5a);
            CHECK(artbox_string_stpcpy(q, p) == q + n);
            CHECK(equal((unsigned char*)p, (unsigned char*)q, n + 1));
            CHECK(filled(b, bi, 0x5a) && filled((unsigned char*)q + n + 1, page - bi - n - 1, 0x5a));
            ++cases;
        }
    }
    for (size_t n = 0; n <= 512; ++n) {
        for (size_t shift = 0; shift < 32; ++shift) {
            for (unsigned right = 0; right < 2; ++right) {
                size_t start = page - n - shift;
                size_t from = right ? start : start + shift;
                size_t to = right ? start + shift : start;
                for (size_t i = 0; i < page; ++i) a[i] = b[i] = (unsigned char)i;
                /* Read the original from a; write the independent expected b. */
                for (size_t i = 0; i < n; ++i) b[to + i] = a[from + i];
                CHECK(artbox_string_memmove(a + to, a + from, n) == a + to);
                CHECK(equal(a, b, page));
                ++cases;
            }
        }
    }
    return (int64_t)cases;
}
