#ifndef ARTBOX_NATIVE_HELLO_H
#define ARTBOX_NATIVE_HELLO_H
#include "artbox/runtime.h"

/* Run the bundled, signed M1 fixture and verify its five-syscall result.
 * The Apple implementation opens the library with dyld; it creates no code. */
int artbox_run_native_hello(const char *library, size_t image_size, const artbox_host *host);
#endif
