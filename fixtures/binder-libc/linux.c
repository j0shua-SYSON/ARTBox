/* SPDX-License-Identifier: MIT */
#include "artbox/binder_libc_result.h"
#include <stdio.h>

int main(void) {
    int result = artbox_binder_libc_check(0);
    int path = artbox_binder_libc_check(1);
    int clock = artbox_binder_libc_check(2);
    int regex = artbox_binder_regex_check(0);
    int newline = artbox_binder_regex_check(1);
    int capture = artbox_binder_regex_check(2);
    printf("{\"cases\":%d,\"path_control\":%d,\"clock_control\":%d,"
           "\"regex_cases\":%d,\"regex_newline_control\":%d,\"regex_capture_control\":%d}\n",
           result, path, clock, regex, newline, capture);
    return result != ARTBOX_BINDER_LIBC_CASES || path != ARTBOX_BINDER_LIBC_PATH_CONTROL ||
           clock != ARTBOX_BINDER_LIBC_CLOCK_CONTROL || regex != ARTBOX_BINDER_REGEX_CASES ||
           newline != ARTBOX_BINDER_REGEX_NEWLINE_CONTROL || capture != ARTBOX_BINDER_REGEX_CAPTURE_CONTROL;
}
