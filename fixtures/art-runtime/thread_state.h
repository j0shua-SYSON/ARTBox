// SPDX-License-Identifier: MIT
#ifndef ARTBOX_ART_THREAD_STATE_H
#define ARTBOX_ART_THREAD_STATE_H
#include <stddef.h>
#include <stdint.h>

// Implemented inside libart so its hidden inline TLS variable is the runtime's
// actual instance, not another copy instantiated in the invocation harness.
extern "C" bool artbox_art_thread_state(void* expected_jni, uintptr_t* thread,
                                       size_t** sampler_slot);
#endif
