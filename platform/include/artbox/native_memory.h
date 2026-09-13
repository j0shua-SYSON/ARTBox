#ifndef ARTBOX_NATIVE_MEMORY_H
#define ARTBOX_NATIVE_MEMORY_H
#include "artbox/guest.h"
#ifdef __cplusplus
extern "C" {
#endif
artbox_memory_ops artbox_native_memory(void);
/* Available only on native Apple arm64. This precompiled bridge changes the
 * stack/register boundary and branches to already-signed code. */
void artbox_enter_arm64(const void *entry, artbox_guest *guest,
                        artbox_dispatch_fn dispatch, void *stack);
#ifdef __cplusplus
}
#endif
#endif
