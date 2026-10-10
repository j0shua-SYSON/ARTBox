// SPDX-License-Identifier: MIT
#ifndef ARTBOX_RUNTIME_EXTENSION_H
#define ARTBOX_RUNTIME_EXTENSION_H
#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include "artbox/vm.h"

// Internal diagnostic extension, compiled for Android on both sides. The
// callback runs on the attached Bionic VM worker after M3's managed checks.
// Its result is returned after interpreter-policy and VM-shutdown checks.
// class_path is the complete -Djava.class.path option, with the M3 fixtures.
// All members remain alive until the one-shot entry returns. No JNI C++ or
// varargs calls cross into the Apple ABI.
struct artbox_runtime_extension {
  const char* class_path;
  int (*run)(JavaVM*, JNIEnv*, void*);
  void* context;
};

extern "C" int artbox_native_runtime_with_extension(artbox_vm* owner, size_t page,
    uint64_t* metrics, const artbox_runtime_extension* extension);
#endif
