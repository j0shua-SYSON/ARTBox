// Original ARTBox diagnostic. MIT. This is a Linux reference, not Apple acceptance.
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

int main(int argc, char** argv) {
  if (argc != 3) {
    fputs("usage: art-linux-reference boot-class-path hello.dex\n", stderr);
    return 64;
  }
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::string boot = std::string("-Xbootclasspath:") + argv[1];
  std::string app = std::string("-Djava.class.path=") + argv[2];
  const char* values[] = {"-Xint", "-Xusejit:false", "-Xuseprofiledjit:false",
    "-Xnoimage-dex2oat", "-Ximage:/nonexistent/artbox-boot.art", "-Xms16m", "-Xmx128m",
    boot.c_str(), app.c_str()};
  JavaVMOption options[sizeof(values) / sizeof(values[0])]{};
  for (size_t i = 0; i != sizeof(values) / sizeof(values[0]); ++i)
    options[i].optionString = const_cast<char*>(values[i]);
  JavaVMInitArgs args{JNI_VERSION_1_6, static_cast<jint>(sizeof(values) / sizeof(values[0])),
    options, JNI_FALSE};
  JavaVM* vm = nullptr;
  JNIEnv* env = nullptr;
  puts("ARTBox: entering original ART JNI_CreateJavaVM");
  jint result = JNI_CreateJavaVM(&vm, &env, &args);
  if (result != JNI_OK || !vm || !env) {
    fprintf(stderr, "ARTBox: ART startup failed: %d\n", result);
    return 2;
  }
  puts("ARTBox: ART started");
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
  return vm->DestroyJavaVM() == JNI_OK ? 0 : 8;
}
