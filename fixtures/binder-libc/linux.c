/* SPDX-License-Identifier: MIT */
#include "artbox/binder_libc_result.h"
#include <stdio.h>

int main(void) {
    int result = artbox_binder_libc_check(0);
    int path = artbox_binder_libc_check(1);
    int clock = artbox_binder_libc_check(2);
    printf("{\"cases\":%d,\"path_control\":%d,\"clock_control\":%d}\n", result, path, clock);
    return result != ARTBOX_BINDER_LIBC_CASES || path != ARTBOX_BINDER_LIBC_PATH_CONTROL ||
           clock != ARTBOX_BINDER_LIBC_CLOCK_CONTROL;
}
