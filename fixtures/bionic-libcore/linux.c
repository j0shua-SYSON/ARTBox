/* SPDX-License-Identifier: MIT */
#include <stdio.h>
extern int artbox_libcore_frontend_common(void);
int main(void) {
    int result = artbox_libcore_frontend_common();
    printf("{\"cases\":45,\"result\":%d}\n", result);
    return result != 45;
}
