// SPDX-License-Identifier: MIT
#ifndef ARTBOX_FRAMEWORK_QUEUE_CHECK_H
#define ARTBOX_FRAMEWORK_QUEUE_CHECK_H
#include <jni.h>
#include <stdint.h>
struct artbox_framework_queue_result {
    uint32_t checks, failure, attachments, wake_calls;
    uint32_t disposed, native_queue_released, looper_released;
    uint64_t poll_nanoseconds;
};
// Runs inside the Android ABI on a real, attached ART thread with no Looper.
// mutation 1 omits nativeWake to prove the blocked-poll observation matters.
int artbox_framework_queue_check(JavaVM *vm, JNIEnv *env, unsigned mutation,
                                artbox_framework_queue_result *result);
#endif
