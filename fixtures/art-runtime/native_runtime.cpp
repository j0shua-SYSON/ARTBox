// Original signed ART execution acceptance. SPDX-License-Identifier: MIT
#include <jni.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include "runtime.h"
#include "instrumentation.h"
#include "jit/jit_options.h"
#include "gc/heap.h"
#include "artbox_art_heap.h"
#include "record_guest.h"

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
static int run_runtime(artbox_vm* owner, size_t page, uint64_t* metrics) {
  // The 128 MiB Java maximum does not require the full 4 GiB reference range.
  // Leave room for ART's separate spaces within a 1536 MiB shared VM budget.
  constexpr uint64_t kManagedWindowBytes = UINT64_C(512) << 20;
  const uint64_t reserved_before = artbox_vm_reserved_bytes(owner);
  const int binding = artbox_art_heap_initialize(owner, kManagedWindowBytes, page);
  if (binding) {
    fprintf(stderr, "ARTBox managed arena: bytes=%llu reserved_before=%llu error=%d\n",
            static_cast<unsigned long long>(kManagedWindowBytes),
            static_cast<unsigned long long>(reserved_before), binding);
    return 76;
  }
  metrics[2] = artbox_art_heap_window().length;
  if (metrics[2] != kManagedWindowBytes) return 76;
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
  if (!ARTBOX_RUNTIME_RECORD("ARTBox: entering signed ART JNI_CreateJavaVM")) return 79;
  uint64_t before = monotonic_ns();
  jint created = JNI_CreateJavaVM(&vm, &env, &args);
  uint64_t after = monotonic_ns();
  if (created != JNI_OK || !vm || !env) {
    fprintf(stderr, "ARTBox: signed ART startup failed: %d\n", created);
    return 2;
  }
  if (!before || after <= before || !interpreter_policy()) return 67;
  metrics[0] = after - before;
  if (!ARTBOX_RUNTIME_RECORD("ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache")) return 79;
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
  bool printed = ARTBOX_RUNTIME_RECORD("%s", text);
  env->ReleaseStringUTFChars(value, text);
  if (!matched) return 7;
  if (!printed || !ARTBOX_RUNTIME_RECORD("ARTBox: signed ART method returned the expected string")) return 79;
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
  if (!ARTBOX_RUNTIME_RECORD("ARTBox: signed ART lifecycle checks passed")) return 79;
  return 0;
}

namespace {
constexpr size_t kVmStackBytes = 4 * 1024 * 1024;
struct RuntimeWorker {
  artbox_vm* owner;
  size_t page;
  uint64_t* metrics;
  int result = 77;
};

void* runtime_worker(void* opaque) {
  auto* worker = static_cast<RuntimeWorker*>(opaque);
  // Use the actual Bionic pthread attributes that ART's GetThreadStack reads.
  // The primordial thread instead requires Linux resource/proc metadata.
  pthread_attr_t attr;
  if (gettid() == getpid() || pthread_getattr_np(pthread_self(), &attr)) return nullptr;
  void* base = nullptr;
  size_t size = 0, guard = 0;
  bool valid = pthread_attr_getstack(&attr, &base, &size) == 0 &&
      pthread_attr_getguardsize(&attr, &guard) == 0;
  if (pthread_attr_destroy(&attr)) valid = false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(base);
  const uintptr_t current = reinterpret_cast<uintptr_t>(&attr);
  valid = valid && base && size > guard && size - guard >= PTHREAD_STACK_MIN &&
      current >= start && current - start >= guard && current - start < size;
  if (!valid) return nullptr;
  if (!ARTBOX_RUNTIME_RECORD("ARTBox VM worker: {\"primordial\":false,\"requested_stack_bytes\":%zu,"
         "\"reported_stack_bytes\":%zu,\"guard_bytes\":%zu,\"current_in_stack\":true}",
         kVmStackBytes, size, guard)) { worker->result = 79; return nullptr; }
  worker->result = run_runtime(worker->owner, worker->page, worker->metrics);
  return nullptr;
}
}  // namespace

// Output words are startup nanoseconds, managed bytes and arena reservation.
extern "C" int artbox_native_runtime_check(artbox_vm* owner, size_t page, uint64_t* metrics) {
  if (!owner || !metrics) return -22;
  metrics[0] = metrics[1] = metrics[2] = 0;
  setvbuf(stdout, nullptr, _IONBF, 0);
  RuntimeWorker worker{owner, page, metrics};
  pthread_attr_t attr;
  if (pthread_attr_init(&attr)) return 78;
  size_t requested = 0;
  bool valid = pthread_attr_setstacksize(&attr, kVmStackBytes) == 0 &&
      pthread_attr_getstacksize(&attr, &requested) == 0 && requested == kVmStackBytes;
  pthread_t thread;
  const int created = valid ? pthread_create(&thread, &attr, runtime_worker, &worker) : -1;
  const int destroyed = pthread_attr_destroy(&attr);
  if (created) return 78;
  // Never release the worker's arguments or heap owner while it can still run.
  if (pthread_join(thread, nullptr)) abort();
  return destroyed ? 79 : worker.result;
}
