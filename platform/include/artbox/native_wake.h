#ifndef ARTBOX_NATIVE_WAKE_H
#define ARTBOX_NATIVE_WAKE_H
#include "artbox/wake.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Darwin kqueue user event, Linux eventfd, Windows auto-reset event. */
artbox_wake_ops artbox_native_wake(void);
#ifdef __cplusplus
}
#endif
#endif
