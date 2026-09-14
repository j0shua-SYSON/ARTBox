#ifndef ARTBOX_NATIVE_BIONIC_H
#define ARTBOX_NATIVE_BIONIC_H
#include "artbox/runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_bionic_input {
    /* libc, NDK client, versioned provider, TLS provider, in that order. */
    const char *frameworks[4], *elfs[4];
    const char *root;
    unsigned sampled;
} artbox_bionic_input;
/* Diagnostic acceptance runner, one call per process. The supplied root must
 * contain fresh data and system directories. Runs on a background caller and
 * joins its guest workers before returning. Emits JSON through the log callback.
 * As in the host test, unexpected native faults or contract failures terminate
 * the process. This is not a recoverable production APK runtime interface. */
int artbox_run_native_bionic(const artbox_bionic_input *input, const artbox_host *host);
#ifdef __cplusplus
}
#endif
#endif
