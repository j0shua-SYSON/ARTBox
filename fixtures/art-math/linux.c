// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 3; }
    uint32_t (*check)(void) = NULL, (*count)(void) = NULL;
    void *symbol = dlsym(library, "artbox_math_check");
    _Static_assert(sizeof(check) == sizeof(symbol), "native function address width");
    memcpy(&check, &symbol, sizeof(check));
    symbol = dlsym(library, "artbox_math_case_count");
    memcpy(&count, &symbol, sizeof(count));
    if (!check || !count) return 4;
    uint32_t cases = count(), failure = check();
    if (cases != 78 || failure) {
        fprintf(stderr, "math cases %u, first failure %u\n", cases, failure);
        return 5;
    }
    if (dlclose(library)) return 6;
    puts("{\"cases\":78,\"first_failure\":0,\"cleanup\":true}");
    return 0;
}
