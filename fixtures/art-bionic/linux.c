// SPDX-License-Identifier: MIT
#include <stdio.h>
int artbox_art_bionic_check(void);
int main(void) {
    int result = artbox_art_bionic_check();
    if (result != 30) { fprintf(stderr, "ART libc check: %d\n", result); return 1; }
    puts("{\"cases\":30,\"result\":30}");
    return 0;
}
