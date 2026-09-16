// SPDX-License-Identifier: MIT
// Original native ART acceptance probe. A compiled probe is not execution evidence.
#include <jni.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static bool no_exception(JNIEnv* env) {
  if (!env->ExceptionCheck()) return true;
  env->ExceptionDescribe();
  env->ExceptionClear();
  return false;
}

static bool gc_count(JNIEnv* env, jclass debug, jmethodID method, jstring key,
                     unsigned long long* result) {
  jstring value = static_cast<jstring>(env->CallStaticObjectMethod(debug, method, key));
  if (!no_exception(env) || !value) return false;
  const char* text = env->GetStringUTFChars(value, nullptr);
  if (!text) { env->DeleteLocalRef(value); no_exception(env); return false; }
  char* end = nullptr;
  errno = 0;
  unsigned long long number = strtoull(text, &end, 10);
  const bool valid = text[0] >= '0' && text[0] <= '9' && *end == '\0' && errno == 0;
  env->ReleaseStringUTFChars(value, text);
  env->DeleteLocalRef(value);
  if (valid) *result = number;
  return valid;
}

struct Worker {
  JavaVM* vm;
  jclass fixture;
  jmethodID method;
  const char* name;
  bool passed;
};

static void* run_worker(void* opaque) {
  Worker* worker = static_cast<Worker*>(opaque);
  for (int cycle = 0; cycle < 2; ++cycle) {
    void* current = nullptr;
    if (worker->vm->GetEnv(&current, JNI_VERSION_1_6) != JNI_EDETACHED) return nullptr;
    JNIEnv* env = nullptr;
    JavaVMAttachArgs args{JNI_VERSION_1_6, const_cast<char*>(worker->name), nullptr};
    if (worker->vm->AttachCurrentThread(&env, &args) != JNI_OK || !env) return nullptr;
    current = nullptr;
    bool valid = worker->vm->GetEnv(&current, JNI_VERSION_1_6) == JNI_OK && current == env;
    jstring name = env->NewStringUTF(worker->name);
    if (name && no_exception(env)) {
      const jint ordinal = env->CallStaticIntMethod(worker->fixture, worker->method, name);
      valid = no_exception(env) && ordinal >= 1 && ordinal <= 4 && valid;
    } else {
      no_exception(env);
      valid = false;
    }
    if (name) env->DeleteLocalRef(name);
    valid = worker->vm->DetachCurrentThread() == JNI_OK && valid;
    current = nullptr;
    valid = worker->vm->GetEnv(&current, JNI_VERSION_1_6) == JNI_EDETACHED && valid;
    if (!valid) return nullptr;
  }
  worker->passed = true;
  return nullptr;
}

bool artbox_run_managed_checks(JavaVM* vm, JNIEnv* env) {
  if (env->PushLocalFrame(16) != JNI_OK) { no_exception(env); return false; }
  struct LocalFrame {
    JNIEnv* env;
    ~LocalFrame() { env->PopLocalFrame(nullptr); }
  } frame{env};
  jclass fixture = env->FindClass("artbox/RuntimeChecks");
  if (!no_exception(env) || !fixture) return false;
  jmethodID heap = env->GetStaticMethodID(fixture, "heapAndDispatch", "()I");
  if (!no_exception(env) || !heap) return false;
  jmethodID exceptions = env->GetStaticMethodID(fixture, "exceptions", "()I");
  if (!no_exception(env) || !exceptions) return false;
  jmethodID attached = env->GetStaticMethodID(fixture, "attachedThread", "(Ljava/lang/String;)I");
  if (!no_exception(env) || !attached) return false;
  jmethodID calls = env->GetStaticMethodID(fixture, "threadCalls", "()I");
  if (!no_exception(env) || !calls) return false;
  jclass debug = env->FindClass("dalvik/system/VMDebug");
  if (!no_exception(env) || !debug) return false;
  jmethodID stat = env->GetStaticMethodID(debug, "getRuntimeStat", "(Ljava/lang/String;)Ljava/lang/String;");
  if (!no_exception(env) || !stat) return false;
  jstring key = env->NewStringUTF("art.gc.gc-count");
  if (!no_exception(env) || !key) return false;
  unsigned long long before = 0, after = 0;
  if (!gc_count(env, debug, stat, key, &before)) return false;
  puts("ARTBox: running managed allocation, collection and dispatch checks");
  jint heap_result = env->CallStaticIntMethod(fixture, heap);
  if (!no_exception(env) || heap_result != 6496) return false;
  if (!gc_count(env, debug, stat, key, &after) || after <= before) return false;
  printf("ARTBox: managed heap checksum %d; collections %llu -> %llu\n", heap_result, before, after);
  jint caught = env->CallStaticIntMethod(fixture, exceptions);
  if (!no_exception(env) || caught != 3) return false;
  puts("ARTBox: managed null and bounds exceptions passed");
  env->DeleteLocalRef(key);
  env->DeleteLocalRef(debug);

  jclass global = static_cast<jclass>(env->NewGlobalRef(fixture));
  if (!no_exception(env) || !global) return false;
  Worker workers[] = {{vm, global, attached, "ARTBox-worker-0", false},
                      {vm, global, attached, "ARTBox-worker-1", false}};
  pthread_t threads[2];
  size_t started = 0;
  for (; started < 2; ++started) {
    if (pthread_create(&threads[started], nullptr, run_worker, &workers[started]) != 0) break;
  }
  bool joined = true;
  for (size_t i = 0; i < started; ++i) joined = pthread_join(threads[i], nullptr) == 0 && joined;
  // A failed join cannot safely release the workers' shared global reference.
  if (!joined) abort();
  env->DeleteGlobalRef(global);
  if (started != 2 || !workers[0].passed || !workers[1].passed) return false;
  jint total = env->CallStaticIntMethod(fixture, calls);
  if (!no_exception(env) || total != 4) return false;
  env->DeleteLocalRef(fixture);
  printf("ARTBox managed checks: {\"heap_checksum\":%d,\"exceptions\":%d,"
         "\"gc_before\":%llu,\"gc_after\":%llu,\"attachments\":%d}\n",
         heap_result, caught, before, after, total);
  return true;
}
