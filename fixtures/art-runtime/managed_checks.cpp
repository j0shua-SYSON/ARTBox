// SPDX-License-Identifier: MIT
// Original native ART acceptance probe. A compiled probe is not execution evidence.
#include <jni.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "thread_state.h"

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

static void checked_pthread(int result) {
  if (result != 0) abort();
}

struct ThreadGate {
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
  size_t ready = 0;
  bool released = false;
  bool valid = false;
  ~ThreadGate() {
    checked_pthread(pthread_cond_destroy(&condition));
    checked_pthread(pthread_mutex_destroy(&mutex));
  }
  bool arrive() {
    checked_pthread(pthread_mutex_lock(&mutex));
    ++ready;
    checked_pthread(pthread_cond_broadcast(&condition));
    while (!released) checked_pthread(pthread_cond_wait(&condition, &mutex));
    const bool result = valid;
    checked_pthread(pthread_mutex_unlock(&mutex));
    return result;
  }
};

struct RestoreSampler {
  size_t* slot;
  size_t saved;
  ~RestoreSampler() { if (slot) *slot = saved; }
};

struct Worker {
  JavaVM* vm;
  jclass fixture;
  jmethodID method;
  const char* name;
  bool passed;
  ThreadGate* gate;
  uintptr_t main_thread;
  size_t cookie;
  size_t* sampler_slot = nullptr;
  bool prepared = false;
};

static void* run_worker(void* opaque) {
  Worker* worker = static_cast<Worker*>(opaque);
  uintptr_t detached = 0;
  worker->prepared = artbox_art_thread_state(nullptr, &detached, &worker->sampler_slot) &&
      detached == 0 && *worker->sampler_slot == 0;
  RestoreSampler restore{worker->sampler_slot, worker->sampler_slot ? *worker->sampler_slot : 0};
  if (worker->prepared) *worker->sampler_slot = worker->cookie;
  // Both native threads remain alive until the main thread compares their TLS
  // addresses and contents; terminated-thread storage reuse cannot pass this test.
  if (!worker->gate->arrive()) return nullptr;
  for (int cycle = 0; cycle < 2; ++cycle) {
    void* current = nullptr;
    if (worker->vm->GetEnv(&current, JNI_VERSION_1_6) != JNI_EDETACHED) return nullptr;
    JNIEnv* env = nullptr;
    JavaVMAttachArgs args{JNI_VERSION_1_6, const_cast<char*>(worker->name), nullptr};
    if (worker->vm->AttachCurrentThread(&env, &args) != JNI_OK || !env) return nullptr;
    current = nullptr;
    bool valid = worker->vm->GetEnv(&current, JNI_VERSION_1_6) == JNI_OK && current == env;
    uintptr_t attached = 0;
    size_t* sampler_slot = nullptr;
    valid = artbox_art_thread_state(env, &attached, &sampler_slot) && attached != 0 &&
        attached != worker->main_thread && sampler_slot == worker->sampler_slot &&
        *sampler_slot == worker->cookie + cycle && valid;
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
    sampler_slot = nullptr;
    valid = artbox_art_thread_state(nullptr, &detached, &sampler_slot) && detached == 0 &&
        sampler_slot == worker->sampler_slot && *sampler_slot == worker->cookie + cycle && valid;
    if (!valid) return nullptr;
    ++*sampler_slot;
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
  uintptr_t main_thread = 0;
  size_t* main_slot = nullptr;
  if (!artbox_art_thread_state(env, &main_thread, &main_slot) || !main_thread) {
    env->DeleteGlobalRef(global);
    return false;
  }
  RestoreSampler restore{main_slot, *main_slot};
  constexpr size_t main_cookie = 0x4d41494e;
  *main_slot = main_cookie;
  ThreadGate gate;
  Worker workers[] = {{vm, global, attached, "ARTBox-worker-0", false, &gate, main_thread, 0x1000},
                      {vm, global, attached, "ARTBox-worker-1", false, &gate, main_thread, 0x2000}};
  pthread_t threads[2];
  size_t started = 0;
  for (; started < 2; ++started) {
    if (pthread_create(&threads[started], nullptr, run_worker, &workers[started]) != 0) break;
  }
  checked_pthread(pthread_mutex_lock(&gate.mutex));
  while (gate.ready < started) checked_pthread(pthread_cond_wait(&gate.condition, &gate.mutex));
  gate.valid = started == 2 && workers[0].prepared && workers[1].prepared &&
      workers[0].sampler_slot != workers[1].sampler_slot &&
      workers[0].sampler_slot != main_slot && workers[1].sampler_slot != main_slot &&
      *workers[0].sampler_slot == workers[0].cookie &&
      *workers[1].sampler_slot == workers[1].cookie && *main_slot == main_cookie;
  gate.released = true;
  checked_pthread(pthread_cond_broadcast(&gate.condition));
  checked_pthread(pthread_mutex_unlock(&gate.mutex));
  bool joined = true;
  for (size_t i = 0; i < started; ++i) joined = pthread_join(threads[i], nullptr) == 0 && joined;
  // A failed join cannot safely release the workers' shared global reference.
  if (!joined) abort();
  env->DeleteGlobalRef(global);
  if (started != 2 || !workers[0].passed || !workers[1].passed) return false;
  uintptr_t current_thread = 0;
  size_t* current_slot = nullptr;
  if (!artbox_art_thread_state(env, &current_thread, &current_slot) ||
      current_thread != main_thread || current_slot != main_slot || *main_slot != main_cookie) return false;
  *main_slot = restore.saved;
  if (!artbox_art_thread_state(env, &current_thread, &current_slot) ||
      current_slot != main_slot || *current_slot != restore.saved) return false;
  jint total = env->CallStaticIntMethod(fixture, calls);
  if (!no_exception(env) || total != 4) return false;
  env->DeleteLocalRef(fixture);
  puts("ARTBox thread state: {\"threads\":3,\"attach_cycles\":4,\"tls_isolated\":true,\"main_tls_restored\":true}");
  printf("ARTBox managed checks: {\"heap_checksum\":%d,\"exceptions\":%d,"
         "\"gc_before\":%llu,\"gc_after\":%llu,\"attachments\":%d}\n",
         heap_result, caught, before, after, total);
  return true;
}
