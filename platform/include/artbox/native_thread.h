#ifndef ARTBOX_NATIVE_THREAD_H
#define ARTBOX_NATIVE_THREAD_H
#include "artbox/threads.h"
#ifdef __cplusplus
extern "C" {
#endif
/* POSIX hosts use public pthread attributes to run on guest-owned RW stacks.
 * Windows has no equivalent public supplied-stack thread API; start returns
 * ENOTSUP there. Portable lifecycle tests use a separate std::thread backend. */
artbox_thread_ops artbox_native_threads(void);
#ifdef __cplusplus
}
#endif
#endif
