/* SPDX-License-Identifier: MIT */
#ifndef ARTBOX_BINDER_LIBC_RESULT_H
#define ARTBOX_BINDER_LIBC_RESULT_H

enum {
    ARTBOX_BINDER_LIBC_CASES = 50,
    ARTBOX_BINDER_LIBC_PATH_CONTROL = -108,
    ARTBOX_BINDER_LIBC_CLOCK_CONTROL = -211,
    ARTBOX_BINDER_REGEX_CASES = 120,
    ARTBOX_BINDER_REGEX_NEWLINE_CONTROL = -1001,
    ARTBOX_BINDER_REGEX_CAPTURE_CONTROL = -2002
};

#ifdef __cplusplus
extern "C" {
#endif
int artbox_binder_libc_check(unsigned mutation);
int artbox_binder_regex_check(unsigned mutation);
#ifdef __cplusplus
}
#endif
#endif
