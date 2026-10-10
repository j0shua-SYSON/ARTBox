// SPDX-License-Identifier: MIT
// Real ART acceptance: original Java MessageQueue and its registered JNI methods.
#include "check.h"
#include "android_os_MessageQueue.h"
#include <pthread.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

namespace android { int register_android_os_MessageQueue(JNIEnv *env); }
namespace {
uint64_t now() {
    timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time)) return 0;
    return uint64_t(time.tv_sec) * UINT64_C(1000000000) + uint64_t(time.tv_nsec);
}
struct Wake {
    JavaVM *vm;
    jclass clazz;
    jlong pointer;
    jmethodID polling, wake;
    bool omit;
    unsigned attached = 0, calls = 0, failure = 0;
};
void *wake_thread(void *opaque) {
    auto &work = *static_cast<Wake *>(opaque);
    JNIEnv *env = nullptr;
    if (work.vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) { work.failure = 201; return nullptr; }
    work.attached = 1;
    const uint64_t start = now();
    bool observed = false;
    while (start && now() - start < UINT64_C(2000000000)) {
        observed = env->CallStaticBooleanMethod(work.clazz, work.polling, work.pointer);
        if (env->ExceptionCheck() || observed) break;
        usleep(1000);
    }
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); work.failure = 202; }
    else if (!observed) work.failure = 203;
    else if (!work.omit) {
        env->CallStaticVoidMethod(work.clazz, work.wake, work.pointer);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); work.failure = 204; }
        else work.calls = 1;
    }
    if (work.vm->DetachCurrentThread() != JNI_OK) work.failure = 205;
    return nullptr;
}
}

int artbox_framework_queue_check(JavaVM *vm, JNIEnv *env, unsigned mutation,
                                artbox_framework_queue_result *result) {
    if (!vm || !env || !result || mutation > 1) return -22;
    *result = {};
    // The fixture owns the native Looper it creates. Do not clear a caller's.
    if (android::Looper::getForThread() != nullptr) return -23;
    jclass clazz = nullptr, global = nullptr;
    jobject queue = nullptr;
    jmethodID dispose = nullptr;
    jfieldID pointer_field = nullptr;
    int descriptor = -1;
    bool joined = true;
    pthread_t thread{};
    Wake wake{};
    android::wp<android::MessageQueue> weak;
    // Preserve failures while still disposing Java/native/thread resources.
    auto check = [&](bool value, unsigned code) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe(); env->ExceptionClear(); value = false;
        }
        if (!value && !result->failure) result->failure = code;
        if (value) ++result->checks;
        return value;
    };
    do {
        clazz = env->FindClass("android/os/MessageQueue");
        if (!check(clazz != nullptr, 101)) break;
        if (!check(android::register_android_os_MessageQueue(env) == 0, 102)) break;
        jmethodID ctor = nullptr, poll = nullptr, polling = nullptr, wake_method = nullptr, events = nullptr;
        // Stop lookup at the first exception; JNI forbids further lookups with
        // an exception pending. Keep registration failure separate from this.
        if (!check((ctor = env->GetMethodID(clazz, "<init>", "(Z)V")) &&
            (dispose = env->GetMethodID(clazz, "dispose", "()V")) &&
            (pointer_field = env->GetFieldID(clazz, "mPtr", "J")) &&
            (poll = env->GetMethodID(clazz, "nativePollOnce", "(JI)V")) &&
            (polling = env->GetStaticMethodID(clazz, "nativeIsPolling", "(J)Z")) &&
            (wake_method = env->GetStaticMethodID(clazz, "nativeWake", "(J)V")) &&
            (events = env->GetStaticMethodID(clazz, "nativeSetFileDescriptorEvents", "(JII)V")), 103)) break;
        queue = env->NewObject(clazz, ctor, JNI_TRUE);
        if (!check(queue != nullptr, 104)) break;
        const jlong pointer = env->GetLongField(queue, pointer_field);
        if (!check(pointer != 0 && android::Looper::getForThread() != nullptr, 105)) break;
        weak = android::android_os_MessageQueue_getMessageQueue(env, queue);
        if (!check(weak.promote() != nullptr, 106)) break;
        if (!check(!env->CallStaticBooleanMethod(clazz, polling, pointer), 107)) break;
        global = static_cast<jclass>(env->NewGlobalRef(clazz));
        if (!check(global != nullptr, 108)) break;
        wake = Wake{vm, global, pointer, polling, wake_method, mutation == 1};
        if (!check(pthread_create(&thread, nullptr, wake_thread, &wake) == 0, 109)) break;
        joined = false;
        uint64_t start = now();
        env->CallVoidMethod(queue, poll, pointer, 3000);
        const uint64_t end = now();
        bool polled = !env->ExceptionCheck();
        if (!check(pthread_join(thread, nullptr) == 0, 110)) break;
        joined = true;
        result->attachments = wake.attached; result->wake_calls = wake.calls;
        result->poll_nanoseconds = end >= start ? end - start : 0;
        if (!check(polled && start && end > start && !wake.failure && wake.attached == 1, 111)) break;
        if (!check(wake.calls == 1 && result->poll_nanoseconds < UINT64_C(2000000000), 112)) break;
        if (!check(!env->CallStaticBooleanMethod(clazz, polling, pointer), 113)) break;
        descriptor = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (!check(descriptor >= 0, 114)) break;
        // Register then remove before signalling. A retained registration would
        // enter Java dispatchEvents with no listener and fail, or wake the poll.
        env->CallStaticVoidMethod(clazz, events, pointer, descriptor, 1);
        if (!check(true, 115)) break;
        env->CallStaticVoidMethod(clazz, events, pointer, descriptor, 0);
        if (!check(true, 116)) break;
        // Drain the Looper's registration wake before testing the removed fd.
        env->CallVoidMethod(queue, poll, pointer, 0);
        if (!check(true, 117)) break;
        uint64_t one = 1;
        if (!check(write(descriptor, &one, sizeof(one)) == sizeof(one), 118)) break;
        start = now(); env->CallVoidMethod(queue, poll, pointer, 30);
        if (!check(start && now() - start >= UINT64_C(20000000), 119)) break;
    } while (false);
    if (!joined) {
        // nativePollOnce is bounded, and this join owns the reference lifetime.
        if (pthread_join(thread, nullptr) != 0) std::abort();
    }
    if (descriptor >= 0) check(close(descriptor) == 0, 120);
    if (queue && dispose && pointer_field) {
        env->CallVoidMethod(queue, dispose);
        result->disposed = check(!env->ExceptionCheck() && env->GetLongField(queue, pointer_field) == 0, 121);
        result->native_queue_released = check(weak.promote() == nullptr, 122);
        if (result->disposed) {
            env->CallVoidMethod(queue, dispose);
            check(!env->ExceptionCheck() && env->GetLongField(queue, pointer_field) == 0, 123);
        }
    }
    if (queue) env->DeleteLocalRef(queue);
    if (global) env->DeleteGlobalRef(global);
    if (clazz) env->DeleteLocalRef(clazz);
    android::Looper::setForThread(android::sp<android::Looper>());
    result->looper_released = check(android::Looper::getForThread() == nullptr, 124);
    return result->failure ? -static_cast<int>(result->failure) : 0;
}
