// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_ART_H
#define ARTBOX_NATIVE_ART_H
#include "artbox/runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_art_input {
    /* libc, libart, libm, libdl: verified signed images and their original ELFs. */
    const char *frameworks[4], *elfs[4];
    const char *root;
} artbox_art_input;
/* One-shot diagnostic: relocate the complete guest, initialize Bionic and ELF
 * constructors, query ART's pre-start JNI state and bind its shared heap.
 * Does not start a JavaVM or execute DEX. Unexpected failures terminate the
 * process, as in the Bionic acceptance runner. No physical-device claim. */
int artbox_run_native_art_bootstrap(const artbox_art_input *input, const artbox_host *host);
#ifdef __cplusplus
}
#endif
#endif
