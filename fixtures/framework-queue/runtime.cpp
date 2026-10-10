// SPDX-License-Identifier: MIT
#include "check.h"
#include "extension.h"
#include "record_guest.h"
#include <inttypes.h>

namespace {
bool record(const char* mode, int status, const artbox_framework_queue_result& value) {
    return ARTBOX_RUNTIME_RECORD("ARTBox framework queue: {\"mode\":\"%s\",\"status\":%d,"
        "\"checks\":%u,\"failure\":%u,\"attachments\":%u,\"wake_calls\":%u,"
        "\"disposed\":%u,\"native_queue_released\":%u,\"looper_released\":%u,"
        "\"poll_ns\":%" PRIu64 "}", mode, status, value.checks, value.failure,
        value.attachments, value.wake_calls, value.disposed, value.native_queue_released,
        value.looper_released, value.poll_nanoseconds);
}
bool released(const artbox_framework_queue_result& value) {
    return value.disposed == 1 && value.native_queue_released == 1 && value.looper_released == 1;
}
int run(JavaVM* vm, JNIEnv* env, void*) {
    artbox_framework_queue_result positive{}, control{};
    int status = artbox_framework_queue_check(vm, env, 0, &positive);
    if (!record("wake", status, positive)) return -301;
    if (status || positive.failure || positive.checks != 24 || positive.attachments != 1 ||
        positive.wake_calls != 1 || !released(positive)) return -302;
    status = artbox_framework_queue_check(vm, env, 1, &control);
    if (!record("omit-wake", status, control)) return -303;
    if (status != -112 || control.failure != 112 || control.checks != 15 || control.attachments != 1 ||
        control.wake_calls != 0 || control.poll_nanoseconds < UINT64_C(2500000000) || !released(control)) return -304;
    return 0;
}
}

extern "C" int artbox_framework_runtime_check(artbox_vm* owner, size_t page, uint64_t* metrics) {
    const artbox_runtime_extension extension{
        "-Djava.class.path=/data/hello.dex:/data/runtime-checks.dex:/data/framework-queue.dex", run, nullptr};
    return artbox_native_runtime_with_extension(owner, page, metrics, &extension);
}
