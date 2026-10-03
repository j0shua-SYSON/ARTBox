// SPDX-License-Identifier: MIT
// Original regression for libc routines required by ART's Android dependency
// graph. Filesystem mutations, signal delivery and process creation have their
// own kernel contracts; compiling their Bionic wrappers does not implement them.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <wchar.h>

#define CHECK(condition) do { if (!(condition)) return -__LINE__; ++cases; } while (0)
static unsigned comparisons;
static int compare(const void *left, const void *right) {
    int a = *(const int *)left, b = *(const int *)right;
    ++comparisons;
    return (a > b) - (a < b);
}
int artbox_art_bionic_check(void) {
    int cases = 0;
    int values[] = {INT_MAX, 0, -1, INT_MIN, 7, -1};
    const int expected[] = {INT_MIN, -1, -1, 0, 7, INT_MAX};
    comparisons = 0;
    qsort(values, 6, sizeof(int), compare);
    CHECK(!memcmp(values, expected, sizeof(values)) && comparisons > 0);
    comparisons = 0;
    qsort(values, 0, sizeof(int), compare);
    CHECK(comparisons == 0 && !memcmp(values, expected, sizeof(values)));

    char path1[] = "/system//bin/", path2[] = "/system/bin/runtime";
    char root[] = "/", leaf[] = "runtime";
    CHECK(!strcmp(basename(path1), "bin"));
    CHECK(!strcmp(dirname(path2), "/system/bin"));
    CHECK(!strcmp(dirname(leaf), "."));
    CHECK(!strcmp(basename(root), "/"));

    locale_t locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    CHECK(locale != (locale_t)0);
    CHECK(strcoll_l("ART", "ART", locale) == 0);
    CHECK(strcoll_l("ART", "Box", locale) < 0);
    char transformed[8] = {0};
    CHECK(strxfrm_l(transformed, "ARTBox", sizeof(transformed), locale) == 6 && !strcmp(transformed, "ARTBox"));
    CHECK(strxfrm_l(transformed, "ARTBox", 0, locale) == 6);
    char *end = NULL;
    errno = 0;
    CHECK(strtoll_l("-9223372036854775808!", &end, 10, locale) == LLONG_MIN && *end == '!' && errno == 0);
    errno = 0;
    CHECK(strtoll_l("9223372036854775808", &end, 10, locale) == LLONG_MAX && *end == 0 && errno == ERANGE);
    errno = 0;
    CHECK(strtoull_l("18446744073709551615!", &end, 10, locale) == ULLONG_MAX && *end == '!' && errno == 0);
    CHECK(strtoull_l("0xff!", &end, 0, locale) == 255 && *end == '!');
    CHECK(strtold_l("1.5!", &end, locale) == 1.5L && *end == '!');
    CHECK(strtold_l("-0.125", &end, locale) == -0.125L && *end == 0);

    const wchar_t wide[] = {L'A', L'R', L'T', 0, L'B'};
    CHECK(wmemchr(wide, L'B', 5) == wide + 4);
    CHECK(wmemchr(wide, L'B', 4) == NULL);
    CHECK(wmemchr(wide, 0, 5) == wide + 3);
    CHECK(wmemcmp(wide, L"ART", 4) == 0);
    CHECK(wmemcmp(wide, L"AS", 2) < 0);
    CHECK(wcscoll_l(L"ART", L"ART", locale) == 0);
    CHECK(wcscoll_l(L"ART", L"Box", locale) < 0);
    wchar_t wide_transform[8] = {0};
    CHECK(wcsxfrm_l(wide_transform, L"ARTBox", 8, locale) == 6 && !wcscmp(wide_transform, L"ARTBox"));
    CHECK(wcsxfrm_l(wide_transform, L"ARTBox", 0, locale) == 6);
    freelocale(locale);

    wchar_t wc = 0;
    CHECK(mbtowc(&wc, "A", 1) == 1 && wc == L'A');
    CHECK(mbtowc(&wc, "", 1) == 0 && wc == 0);
    int sequence[4];
    srand(1234);
    for (unsigned i = 0; i < 4; ++i) sequence[i] = rand();
    srand(1234);
    int same = 1;
    for (unsigned i = 0; i < 4; ++i) same &= rand() == sequence[i];
    CHECK(same);
    CHECK(sequence[0] >= 0 && sequence[0] <= RAND_MAX);

    char joined[16];
    memset(joined, '?', sizeof(joined));
    joined[0] = 0;
    CHECK(strcat(joined, "ART") == joined && !strcmp(joined, "ART") && joined[4] == '?');
    CHECK(strcat(joined, "Box") == joined && !strcmp(joined, "ARTBox") && joined[7] == '?');
    CHECK(strcat(joined, "") == joined && !strcmp(joined, "ARTBox") && joined[7] == '?');
    const char nonterminated[] = {'x', 'y', 'z'};
    // Call the libc symbol itself. Glibc's fortified inline wrapper diagnoses
    // the deliberately bounded source copy as truncation when optimized.
    char *(*volatile append_bounded)(char *, const char *, size_t) = strncat;
    CHECK(append_bounded(joined, nonterminated, 2) == joined && !strcmp(joined, "ARTBoxxy") && joined[9] == '?');
    CHECK(append_bounded(joined, nonterminated, 0) == joined && !strcmp(joined, "ARTBoxxy") && joined[9] == '?');
    CHECK(append_bounded(joined, "!", 3) == joined && !strcmp(joined, "ARTBoxxy!") && joined[10] == '?');
    const int divisions[][4] = {
        {17, 5, 3, 2}, {-17, 5, -3, -2}, {17, -5, -3, 2}, {-17, -5, 3, -2},
        {INT_MIN, 2, INT_MIN / 2, 0}, {INT_MAX, INT_MAX, 1, 0}
    };
    for (unsigned i = 0; i < sizeof(divisions) / sizeof(divisions[0]); ++i) {
        div_t value = div(divisions[i][0], divisions[i][1]);
        CHECK(value.quot == divisions[i][2] && value.rem == divisions[i][3]);
    }

    int key = 7;
    CHECK(bsearch(&key, values, 6, sizeof(int), compare) == values + 4);
    key = 5;
    CHECK(bsearch(&key, values, 6, sizeof(int), compare) == NULL);
    comparisons = 0;
    CHECK(bsearch(&key, values, 0, sizeof(int), compare) == NULL && comparisons == 0);

    struct utsname system_name;
    CHECK(uname(&system_name) == 0);
    char hostname[66];
    memset(hostname, '?', sizeof(hostname));
    CHECK(gethostname(hostname, 65) == 0 && !strcmp(hostname, system_name.nodename) && hostname[65] == '?');
    size_t hostname_length = strlen(system_name.nodename);
    errno = 0;
    CHECK(gethostname(hostname, 0) == -1 && errno == ENAMETOOLONG && hostname[65] == '?');
    memset(hostname, '?', sizeof(hostname));
    errno = 0;
    // Bionic and glibc may leave different bytes inside the failed output.
    CHECK(gethostname(hostname, hostname_length) == -1 && errno == ENAMETOOLONG && hostname[hostname_length] == '?');
    CHECK(gethostname(hostname, hostname_length + 1) == 0 && !strcmp(hostname, system_name.nodename) && hostname[hostname_length + 1] == '?');

    char tokens[] = ",,ART::Box,,", other_tokens[] = "one/two";
    char *saved = NULL, *other_saved = NULL;
    CHECK(!strcmp(strtok_r(tokens, ",:", &saved), "ART"));
    CHECK(!strcmp(strtok_r(other_tokens, "/", &other_saved), "one"));
    CHECK(!strcmp(strtok_r(NULL, ",:", &saved), "Box"));
    CHECK(strtok_r(NULL, ",:", &saved) == NULL);
    CHECK(!strcmp(strtok_r(NULL, "/", &other_saved), "two"));
    CHECK(strtok_r(NULL, "/", &other_saved) == NULL);

    const char *environment_key = "ARTBOX_LIBCORE_ENV_TEST";
    CHECK(setenv(environment_key, "first", 1) == 0 && !strcmp(getenv(environment_key), "first"));
    CHECK(setenv(environment_key, "ignored", 0) == 0 && !strcmp(getenv(environment_key), "first"));
    CHECK(setenv(environment_key, "second", 1) == 0 && !strcmp(getenv(environment_key), "second"));
    CHECK(unsetenv(environment_key) == 0 && getenv(environment_key) == NULL);
    CHECK(unsetenv(environment_key) == 0);
    errno = 0;
    CHECK(setenv("", "value", 1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(setenv("invalid=name", "value", 1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(unsetenv("invalid=name") == -1 && errno == EINVAL);

    unsigned char address[17];
    memset(address, '?', sizeof(address));
    const unsigned char ipv4[] = {192, 0, 2, 1};
    const unsigned char ipv6[] = {0x20, 1, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(inet_pton(AF_INET, "192.0.2.1", address) == 1 && !memcmp(address, ipv4, 4) && address[4] == '?');
    CHECK(inet_pton(AF_INET, "256.0.2.1", address) == 0 && address[4] == '?');
    CHECK(inet_pton(AF_INET6, "2001:db8::1", address) == 1 && !memcmp(address, ipv6, 16) && address[16] == '?');
    CHECK(inet_pton(AF_INET6, "2001::db8::1", address) == 0 && address[16] == '?');
    errno = 0;
    CHECK(inet_pton(AF_UNSPEC, "192.0.2.1", address) == -1 && errno == EAFNOSUPPORT && address[16] == '?');

    union { uint64_t alignment; unsigned char bytes[2 * CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr message = {0};
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    struct cmsghdr *first = CMSG_FIRSTHDR(&message);
    CHECK(first == (struct cmsghdr *)control.bytes);
    first->cmsg_len = CMSG_LEN(sizeof(int));
    struct cmsghdr *second = CMSG_NXTHDR(&message, first);
    CHECK(second == (struct cmsghdr *)(control.bytes + CMSG_SPACE(sizeof(int))));
    second->cmsg_len = CMSG_LEN(sizeof(int));
    CHECK(CMSG_NXTHDR(&message, second) == NULL);
    message.msg_controllen = CMSG_SPACE(sizeof(int));
    CHECK(CMSG_NXTHDR(&message, first) == NULL);
    return cases;
}
