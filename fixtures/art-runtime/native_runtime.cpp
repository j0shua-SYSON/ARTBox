// Original signed ART execution acceptance. SPDX-License-Identifier: MIT
#include <jni.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "runtime.h"
#include "instrumentation.h"
#include "jit/jit_options.h"
#include "gc/heap.h"
#include "artbox_art_heap.h"

bool artbox_run_managed_checks(JavaVM*, JNIEnv*);

static uint64_t monotonic_ns() {
  timespec value{};
  if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
  return uint64_t(value.tv_sec) * UINT64_C(1000000000) + uint64_t(value.tv_nsec);
}
static bool interpreter_policy() {
  art::Runtime* runtime = art::Runtime::Current();
  return runtime && !runtime->GetJit() && !runtime->GetJitCodeCache() &&
      !runtime->GetJITOptions()->UseJitCompilation() &&
      !runtime->GetJITOptions()->GetSaveProfilingInfo() &&
      runtime->GetInstrumentation()->IsForcedInterpretOnly() && !runtime->IsExplicitGcDisabled();
}

// JNI varargs and C++ calls stay inside this Android-compiled signed image.
// Output words are startup nanoseconds and managed bytes before shutdown.
extern "C" int artbox_native_runtime_check(artbox_vm* owner, size_t page, uint64_t* metrics) {
  if (!owner || !metrics) return -22;
  metrics[0] = metrics[1] = 0;
  if (artbox_art_heap_initialize(owner, UINT64_C(0x100000000), page)) return 76;
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* values[] = {"-Xint", "-Xusejit:false", "-Xuseprofiledjit:false",
      "-Xnoimage-dex2oat", "-Ximage:/system/art/artbox-boot.art", "-Xms16m", "-Xmx128m",
      "-Xbootclasspath:/system/framework/classes.dex:/system/framework/classes2.dex",
      "-Djava.class.path=/data/hello.dex:/data/runtime-checks.dex",
      "-Djava.io.tmpdir=/data/scratch", "-Duser.home=/data/scratch"};
  JavaVMOption options[sizeof(values) / sizeof(values[0])]{};
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    options[i].optionString = const_cast<char*>(values[i]);
  JavaVMInitArgs args{JNI_VERSION_1_6, static_cast<jint>(sizeof(values) / sizeof(values[0])),
      options, JNI_FALSE};
  JavaVM* vm = nullptr;
  JNIEnv* env = nullptr;
  puts("ARTBox: entering signed ART JNI_CreateJavaVM");
  uint64_t before = monotonic_ns();
  jint created = JNI_CreateJavaVM(&vm, &env, &args);
  uint64_t after = monotonic_ns();
  if (created != JNI_OK || !vm || !env) {
    fprintf(stderr, "ARTBox: signed ART startup failed: %d\n", created);
    return 2;
  }
  if (!before || after <= before || !interpreter_policy()) return 67;
  metrics[0] = after - before;
  puts("ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache");
  JavaVM* registered = nullptr;
  jsize count = 0;
  if (JNI_GetCreatedJavaVMs(&registered, 1, &count) != JNI_OK || count != 1 || registered != vm) return 70;
  jclass cls = env->FindClass("artbox/Hello");
  if (!cls || env->ExceptionCheck()) { env->ExceptionDescribe(); return 3; }
  jmethodID method = env->GetStaticMethodID(cls, "message", "()Ljava/lang/String;");
  if (!method || env->ExceptionCheck()) { env->ExceptionDescribe(); return 4; }
  jstring value = static_cast<jstring>(env->CallStaticObjectMethod(cls, method));
  if (!value || env->ExceptionCheck()) { env->ExceptionDescribe(); return 5; }
  const char* text = env->GetStringUTFChars(value, nullptr);
  if (!text) { env->ExceptionDescribe(); return 6; }
  bool matched = !strcmp(text, "hello from ARTBox ART");
  puts(text);
  env->ReleaseStringUTFChars(value, text);
  if (!matched) return 7;
  puts("ARTBox: signed ART method returned the expected string");
  if (!artbox_run_managed_checks(vm, env)) return 71;
  if (!interpreter_policy()) return 68;
  metrics[1] = art::Runtime::Current()->GetHeap()->GetBytesAllocated();
  if (!metrics[1]) return 75;
  env->DeleteLocalRef(value);
  env->DeleteLocalRef(cls);
  if (vm->DetachCurrentThread() != JNI_OK) return 72;
  void* detached = nullptr;
  if (vm->GetEnv(&detached, JNI_VERSION_1_6) != JNI_EDETACHED) return 73;
  if (vm->DestroyJavaVM() != JNI_OK) return 8;
  registered = nullptr; count = -1;
  if (JNI_GetCreatedJavaVMs(&registered, 1, &count) != JNI_OK || count != 0) return 74;
  artbox_art_heap_unbind();
  puts("ARTBox: signed ART lifecycle checks passed");
  return 0;
}
