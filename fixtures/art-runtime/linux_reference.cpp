// Original ARTBox diagnostic. MIT. Native Linux reference only.
#include <jni.h>
#include <dlfcn.h>
#include <chrono>
#include "no_codegen.h"
#include "runtime.h"
#include "instrumentation.h"
#include "jit/jit_options.h"
#include "gc/heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/resource.h>

bool artbox_run_managed_checks(JavaVM* vm, JNIEnv* env);

static bool record_maps(const std::string& path) {
  FILE* source = fopen("/proc/self/maps", "r");
  if (!source) return false;
  FILE* destination = fopen(path.c_str(), "w");
  if (!destination) { fclose(source); return false; }
  bool valid = true;
  char line[4096];
  while (fgets(line, sizeof(line), source)) {
    if (fputs(line, destination) < 0) valid = false;
    unsigned long first, last;
    char permissions[5]{};
    if (sscanf(line, "%lx-%lx %4s", &first, &last, permissions) == 3 &&
        permissions[1] == 'w' && permissions[2] == 'x') valid = false;
  }
  if (ferror(source)) valid = false;
  if (fclose(source) != 0) valid = false;
  if (fclose(destination) != 0) valid = false;
  return valid;
}

int main(int argc, char** argv) {
  if (argc != 4) {
    fputs("usage: art-linux-reference boot-class-path app-class-path native-library-directory\n", stderr);
    return 64;
  }
  setvbuf(stdout, nullptr, _IONBF, 0);
  for (const char* name : {"libicu_jni.so", "libjavacore.so", "libopenjdk.so"}) {
    const std::string path = std::string(argv[3]) + "/" + name;
    if (!dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL)) {
      fprintf(stderr, "ARTBox: preload failed: %s\n", dlerror());
      return 65;
    }
  }
  const std::string scratch = std::string(argv[3]) + "/scratch";
  if (!record_maps(scratch + "/maps-before.txt")) return 69;
  if (!artbox_deny_runtime_codegen()) { perror("ARTBox codegen guard"); return 66; }
  auto policy_valid = [] {
    art::Runtime* runtime = art::Runtime::Current();
    return runtime && !runtime->GetJit() && !runtime->GetJitCodeCache() &&
      !runtime->GetJITOptions()->UseJitCompilation() &&
      !runtime->GetJITOptions()->GetSaveProfilingInfo() &&
      runtime->GetInstrumentation()->IsForcedInterpretOnly() && !runtime->IsExplicitGcDisabled();
  };
  std::string boot = std::string("-Xbootclasspath:") + argv[1];
  std::string app = std::string("-Djava.class.path=") + argv[2];
  const std::string temp = "-Djava.io.tmpdir=" + scratch;
  const std::string home = "-Duser.home=" + scratch;
  const std::string missing_image = std::string("-Ximage:") + argv[3] + "/artbox-boot.art";
  const char* values[] = {"-Xint", "-Xusejit:false", "-Xuseprofiledjit:false",
    "-Xnoimage-dex2oat", missing_image.c_str(), "-Xms16m", "-Xmx128m",
    boot.c_str(), app.c_str(), temp.c_str(), home.c_str()};
  JavaVMOption options[sizeof(values) / sizeof(values[0])]{};
  for (size_t i = 0; i != sizeof(values) / sizeof(values[0]); ++i)
    options[i].optionString = const_cast<char*>(values[i]);
  JavaVMInitArgs args{JNI_VERSION_1_6, static_cast<jint>(sizeof(values) / sizeof(values[0])),
    options, JNI_FALSE};
  JavaVM* vm = nullptr;
  JNIEnv* env = nullptr;
  puts("ARTBox: entering original ART JNI_CreateJavaVM");
  const auto start = std::chrono::steady_clock::now();
  jint result = JNI_CreateJavaVM(&vm, &env, &args);
  const auto initialized = std::chrono::steady_clock::now();
  if (result != JNI_OK || !vm || !env) {
    fprintf(stderr, "ARTBox: ART startup failed: %d\n", result);
    return 2;
  }
  if (!policy_valid()) { fputs("ARTBox: runtime policy failed\n", stderr); return 67; }
  JavaVM* registered = nullptr;
  jsize registered_count = 0;
  if (JNI_GetCreatedJavaVMs(&registered, 1, &registered_count) != JNI_OK ||
      registered_count != 1 || registered != vm) return 70;
  const double startup_ms = std::chrono::duration<double, std::milli>(initialized - start).count();
  printf("ARTBox: ART started in %.3f ms; switch interpreter, no JIT, no profiling cache\n", startup_ms);
  jclass cls = env->FindClass("artbox/Hello");
  if (!cls || env->ExceptionCheck()) { env->ExceptionDescribe(); return 3; }
  jmethodID method = env->GetStaticMethodID(cls, "message", "()Ljava/lang/String;");
  if (!method || env->ExceptionCheck()) { env->ExceptionDescribe(); return 4; }
  jstring value = static_cast<jstring>(env->CallStaticObjectMethod(cls, method));
  if (!value || env->ExceptionCheck()) { env->ExceptionDescribe(); return 5; }
  const char* text = env->GetStringUTFChars(value, nullptr);
  if (!text) { env->ExceptionDescribe(); return 6; }
  bool matched = strcmp(text, "hello from ARTBox ART") == 0;
  puts(text);
  env->ReleaseStringUTFChars(value, text);
  if (!matched) return 7;
  puts("ARTBox: real ART method returned the expected string");
  if (!artbox_run_managed_checks(vm, env)) {
    fputs("ARTBox: managed runtime checks failed\n", stderr);
    return 71;
  }
  if (!policy_valid()) return 68;
  if (!record_maps(scratch + "/maps-after.txt")) return 69;
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss <= 0) return 75;
  const size_t managed_bytes = art::Runtime::Current()->GetHeap()->GetBytesAllocated();
  printf("ARTBox runtime memory: {\"managed_allocated_bytes\":%zu,\"process_peak_rss_kib\":%ld}\n",
         managed_bytes, usage.ru_maxrss);
  env->DeleteLocalRef(value);
  env->DeleteLocalRef(cls);
  if (vm->DetachCurrentThread() != JNI_OK) return 72;
  void* detached = nullptr;
  if (vm->GetEnv(&detached, JNI_VERSION_1_6) != JNI_EDETACHED) return 73;
  if (vm->DestroyJavaVM() != JNI_OK) return 8;
  registered = nullptr;
  registered_count = -1;
  if (JNI_GetCreatedJavaVMs(&registered, 1, &registered_count) != JNI_OK || registered_count != 0) return 74;
  if (!record_maps(scratch + "/maps-shutdown.txt")) return 69;
  puts("ARTBox: native ART lifecycle checks passed");
  return 0;
}
