// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_SERVICE_H
#define ARTBOX_NATIVE_SERVICE_H
#include "artbox/runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_service_input {
    /* Each role has separately named signed images: libc, libart (C++ support),
     * libm, libdl, libdl_android and the service payload. ELFs may share bytes;
     * Mach-O instances and writable globals must be distinct. */
    const char *frameworks[3][6], *elfs[3][6], *roots[3];
    const char *backing_root;
    /* 0: three-role acceptance; 1: Access contract; 2/3: wrong UID/PID. */
    unsigned mode;
} artbox_service_input;
/* One-shot diagnostic. Original main is a process-lifetime daemon in mode 0;
 * its owner and images are intentionally retained. The finite provider/client
 * are joined before their descriptor and VM owners are destroyed. No JavaVM,
 * CPU emulation, generated code or privileged host service is involved. */
int artbox_run_native_service(const artbox_service_input *input,const artbox_host *host);
#ifdef __cplusplus
}
#endif
#endif
