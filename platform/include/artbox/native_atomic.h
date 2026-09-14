#ifndef ARTBOX_NATIVE_ATOMIC_H
#define ARTBOX_NATIVE_ATOMIC_H
#include "artbox/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
artbox_atomic_u32_ops artbox_native_atomic_u32(void);
#ifdef __cplusplus
}
#endif
#endif
