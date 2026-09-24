// SPDX-License-Identifier: MIT
// Original regression for libc routines required by ART's Android dependency
// graph. Filesystem mutations, signal delivery and process creation have their
// own kernel contracts; compiling their Bionic wrappers does not implement them.
#define _GNU_SOURCE
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
    return cases;
}
