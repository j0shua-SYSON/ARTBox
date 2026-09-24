// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[2], "original") && strcmp(argv[2], "adapted"))) return 2;
    int expected = !strcmp(argv[2], "original") ? 1 : 0;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 3; }
    void *symbol = dlsym(library, "artbox_check_unwind_context");
    int (*check)(void) = NULL;
    _Static_assert(sizeof(check) == sizeof(symbol), "native function address width");
    memcpy(&check, &symbol, sizeof(check));
    if (!check) return 4;
    int result = check();
    if (result != expected) { fprintf(stderr, "context result %d, expected %d\n", result, expected); return 5; }
    if (dlclose(library)) return 6;
    printf("{\"checks\":4,\"failure_mask\":%d,\"expected_rejection\":%s,\"cleanup\":true}\n",
           result, expected ? "true" : "false");
    return 0;
}
