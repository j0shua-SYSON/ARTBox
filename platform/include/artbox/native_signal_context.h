// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_SIGNAL_CONTEXT_H
#define ARTBOX_NATIVE_SIGNAL_CONTEXT_H
#include "artbox/signal_context.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Apple ARM64 adapter for a live signal callback's borrowed ucontext pointer.
 * No allocation, locks, TLS access or signal disposition changes. These copy
 * architecture state only; guest masks, siginfo and stack routing are separate.
 * Malformed size/PC/SP leaves outputs unchanged. The caller must provide valid
 * owned pointers and validate requested guest code/stack addresses beforehand. */
int artbox_native_signal_capture(const void *context,artbox_arm64_signal_state *state);
/* Preserve the live host x18, non-NZCV CPSR bits and exception information even
 * if the caller supplies different values. Only arm64 builds are supported;
 * arm64e requires a separate authenticated return-address contract. */
int artbox_native_signal_apply(void *context,const artbox_arm64_signal_state *state);
#ifdef __cplusplus
}
#endif
#endif
