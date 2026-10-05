/* SPDX-License-Identifier: MIT */
#include "artbox/binder_libc_result.h"
#include <regex.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(regex_t) == 32 && sizeof(regoff_t) == 8 && sizeof(regmatch_t) == 16,
               "Android LP64 regex ABI changed");
_Static_assert(offsetof(regex_t, re_nsub) == 8 && offsetof(regex_t, re_endp) == 16 &&
               offsetof(regex_t, re_g) == 24, "Android regex field offsets changed");

#define CHECK_AT(id, value) do { if (!(value)) { if (active) regfree(active); return -(id); } ++cases; } while (0)
#define E REG_EXTENDED
#define N REG_NEWLINE

int artbox_binder_regex_check(unsigned mutation) {
    static const struct {
        const char *pattern, *value;
        int compile_flags, execute_flags, expected;
        regoff_t start, end;
    } patterns[] = {
        {"^a.b$", "a\nb", E | N, 0, REG_NOMATCH, 0, 0},
        {"^a.b$", "a\nb", E, 0, 0, 0, 3},
        {"^abc$", "abc", E, 0, 0, 0, 3},
        {"^abc$", "xabc", E, 0, REG_NOMATCH, 0, 0},
        {"abc", "xabcx", E, 0, 0, 1, 4},
        {"^b$", "a\nb\nc", E | N, 0, 0, 2, 3},
        {"^b$", "b", E, REG_NOTBOL, REG_NOMATCH, 0, 0},
        {"^b$", "b", E, REG_NOTEOL, REG_NOMATCH, 0, 0},
        {"^b$", "a\nb\nc", E | N, REG_NOTBOL | REG_NOTEOL, 0, 2, 3},
        {"b.*", "b\nc", E | N, 0, 0, 0, 1},
        {"b.*", "b\nc", E, 0, 0, 0, 3},
        {"^ab[0-9]{2}$", "AB42", E | REG_ICASE, 0, 0, 0, 4},
        {"^ab[0-9]{2}$", "AB42", E, 0, REG_NOMATCH, 0, 0},
        {"(ab|a)b?", "ab", E, 0, 0, 0, 2},
        {"[[:alpha:]]+[[:digit:]]+", "abc42", E, 0, 0, 0, 5},
        {"a{2,3}", "aaaa", E, 0, 0, 0, 3},
        {"a\\{2,3\\}", "aaa", 0, 0, 0, 0, 3},
        {"(ab)", "(ab)", 0, 0, 0, 0, 4},
        {"\\(ab\\)\\1", "abab", 0, 0, 0, 0, 4},
        {"^a[^b]c$", "a\nc", E | N, 0, REG_NOMATCH, 0, 0},
        {"^a[^b]c$", "a\nc", E, 0, 0, 0, 3},
        {"^a*$", "", E, 0, 0, 0, 0},
        {"^a?$", "a", E, 0, 0, 0, 1},
        {"^a+$", "b", E, 0, REG_NOMATCH, 0, 0}
    };
    static const struct { const char *pattern; int flags, code; const char *name; } invalid[] = {
        {"[", E, REG_EBRACK, "REG_EBRACK"},
        {"(", E, REG_EPAREN, "REG_EPAREN"},
        {"a{2,1}", E, REG_BADBR, "REG_BADBR"},
        {"[z-a]", E, REG_ERANGE, "REG_ERANGE"},
        {"[[:bogus:]]", E, REG_ECTYPE, "REG_ECTYPE"},
        {"\\", E, REG_EESCAPE, "REG_EESCAPE"},
        {"*", E, REG_BADRPT, "REG_BADRPT"},
        {"\\1", 0, REG_ESUBREG, "REG_ESUBREG"}
    };
    _Static_assert(3 * sizeof(patterns) / sizeof(patterns[0]) + 6 + 3 + 3 +
                   2 * sizeof(invalid) / sizeof(invalid[0]) + 4 + 16 == ARTBOX_BINDER_REGEX_CASES,
                   "Every regex expectation must remain in the contract");
    if (mutation > 2) return -1;
    const uint64_t guard = UINT64_C(0x8162937455bbccaa);
    int cases = 0;
    regex_t *active = NULL;
    for (unsigned i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        struct { uint64_t before; regex_t value; uint64_t after; } compiled = { guard, {0}, guard };
        struct { uint64_t before; regmatch_t value[2]; uint64_t after; } matches =
            { guard, {{-9, -9}, {-9, -9}}, guard };
        int flags = patterns[i].compile_flags;
        if (mutation == 1) flags &= ~N;
        int rc = regcomp(&compiled.value, patterns[i].pattern, flags);
        if (!rc) active = &compiled.value;
        CHECK_AT(1000 + 3 * (int)i, rc == 0);
        rc = regexec(active, patterns[i].value, 1, matches.value, patterns[i].execute_flags);
        CHECK_AT(1001 + 3 * (int)i, rc == patterns[i].expected &&
                 (rc || (matches.value[0].rm_so == patterns[i].start && matches.value[0].rm_eo == patterns[i].end)));
        regfree(active); active = NULL;
        CHECK_AT(1002 + 3 * (int)i, compiled.before == guard && compiled.after == guard &&
                 matches.before == guard && matches.after == guard &&
                 matches.value[1].rm_so == -9 && matches.value[1].rm_eo == -9);
    }

    regex_t expression = {0};
    int rc = regcomp(&expression, "(ab)(c)?", E);
    if (!rc) active = &expression;
    CHECK_AT(2000, rc == 0 && expression.re_nsub == 2);
    struct { uint64_t before; regmatch_t value[4]; uint64_t after; } capture =
        { guard, {{-9,-9},{-9,-9},{-9,-9},{-9,-9}}, guard };
    CHECK_AT(2001, regexec(active, "xxabyy", mutation == 2 ? 1 : 4, capture.value, 0) == 0);
    CHECK_AT(2002, capture.value[0].rm_so == 2 && capture.value[0].rm_eo == 4 &&
             capture.value[1].rm_so == 2 && capture.value[1].rm_eo == 4);
    CHECK_AT(2003, capture.value[2].rm_so == -1 && capture.value[2].rm_eo == -1 &&
             capture.value[3].rm_so == -1 && capture.value[3].rm_eo == -1);
    CHECK_AT(2004, regexec(active, "abc", 4, capture.value, 0) == 0 &&
             capture.value[0].rm_so == 0 && capture.value[0].rm_eo == 3 &&
             capture.value[1].rm_so == 0 && capture.value[1].rm_eo == 2 &&
             capture.value[2].rm_so == 2 && capture.value[2].rm_eo == 3);
    regfree(active); active = NULL;
    CHECK_AT(2005, capture.before == guard && capture.after == guard);

    rc = regcomp(&expression, "ab", E);
    if (!rc) active = &expression;
    CHECK_AT(2100, rc == 0);
    const char bounded[] = {'x', '\0', 'a', 'b', 'y', '\0'};
    regmatch_t range = {2, 4};
    CHECK_AT(2101, regexec(active, bounded, 1, &range, REG_STARTEND) == 0 && range.rm_so == 2 && range.rm_eo == 4);
    range.rm_so = 0; range.rm_eo = 1;
    CHECK_AT(2102, regexec(active, bounded, 1, &range, REG_STARTEND) == REG_NOMATCH);
    regfree(active); active = NULL;

    rc = regcomp(&expression, "a+", E | REG_NOSUB);
    if (!rc) active = &expression;
    CHECK_AT(2200, rc == 0);
    CHECK_AT(2201, regexec(active, "aaa", 0, NULL, 0) == 0);
    CHECK_AT(2202, regexec(active, "b", 0, NULL, 0) == REG_NOMATCH);
    regfree(active); active = NULL;

    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memset(&expression, 0, sizeof(expression));
        rc = regcomp(&expression, invalid[i].pattern, invalid[i].flags);
        if (!rc) active = &expression;
        CHECK_AT(3000 + 2 * (int)i, rc == invalid[i].code);
        struct { uint64_t before; char value[32]; uint64_t after; } error = { guard, {0}, guard };
        CHECK_AT(3001 + 2 * (int)i, regerror(rc | REG_ITOA, NULL, error.value, sizeof(error.value)) ==
                 strlen(invalid[i].name) + 1 && !strcmp(error.value, invalid[i].name) &&
                 error.before == guard && error.after == guard);
    }
    size_t needed = regerror(REG_NOMATCH, NULL, NULL, 0);
    CHECK_AT(3100, needed > 3 && needed < 128);
    char full[128] = {0};
    CHECK_AT(3101, regerror(REG_NOMATCH, NULL, full, sizeof(full)) == needed && strlen(full) + 1 == needed);
    struct { char before, value[3], after; } short_error = { 'G', {'?', '?', '?'}, 'H' };
    CHECK_AT(3102, regerror(REG_NOMATCH, NULL, short_error.value, sizeof(short_error.value)) == needed &&
             !strncmp(short_error.value, full, 2) && short_error.value[2] == '\0' &&
             short_error.before == 'G' && short_error.after == 'H');
    char untouched = 'G';
    CHECK_AT(3103, regerror(REG_NOMATCH, NULL, &untouched, 0) == needed && untouched == 'G');

    /* Repeat allocation/free with one regex object, including the VINTF caller's
     * distinction between finding a substring and matching the entire value. */
    for (unsigned i = 0; i < 16; ++i) {
        rc = regcomp(&expression, "service[0-9]+", E | N);
        if (!rc) active = &expression;
        int ok = rc == 0;
        regmatch_t match;
        const char *value = i & 1 ? "xservice42x" : "service42";
        if (ok) {
            ok = regexec(active, value, 1, &match, 0) == 0;
            if (ok) {
                int whole = match.rm_so == 0 && match.rm_eo >= 0 && (size_t)match.rm_eo == strlen(value);
                ok = whole == !(i & 1);
            }
            regfree(active); active = NULL;
        }
        CHECK_AT(4000 + (int)i, ok);
    }
    return cases;
}
