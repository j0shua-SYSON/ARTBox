// Copyright (c) ARTBox contributors. MIT license.
#include "jit_create.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unwindstack/Demangle.h>

#if defined(ARTBOX_UPSTREAM_CONTROL)
static unsigned rust_calls;
// Test instrumentation only. Adapted binaries do not define this symbol.
extern "C" char* rustc_demangle(const char*, char*, size_t*, int*) {
  ++rust_calls;
  return nullptr;
}
#endif

extern "C" int artbox_demangle_check() {
  const struct { const char* input; const char* expected; } cases[] = {
    {"", ""}, {"_", "_"}, {"native_method", "native_method"},
    {"_Z3foov", "foo()"}, {"_Z3addii", "add(int, int)"},
    {"_Znot_a_valid_cpp_symbol", "_Znot_a_valid_cpp_symbol"},
    {"_RNvC6_123foo3bar", "_RNvC6_123foo3bar"}, {"_Rinvalid", "_Rinvalid"},
  };
  for (const auto& item : cases) {
    if (unwindstack::DemangleNameIfNeeded(item.input) != item.expected) {
      std::fprintf(stderr, "Unexpected stack trace name: %s\n", item.input);
      return 2;
    }
  }
#if defined(ARTBOX_UPSTREAM_CONTROL)
  if (rust_calls != 0) {
    std::fprintf(stderr, "Optional Rust demangler was invoked %u times\n", rust_calls);
    return 3;
  }
#endif
  std::puts("8 stack trace name cases passed; no Rust runtime invoked");
  return 0;
}

extern "C" void artbox_jit_forbidden_probe() {
  (void)art::jit::jit_create();
  std::fputs("JIT factory returned unexpectedly\n", stderr);
  std::_Exit(3);
}

#if defined(ARTBOX_POLICY_EXECUTABLE)
int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "jit") == 0) {
    artbox_jit_forbidden_probe();
  }
  return artbox_demangle_check();
}
#endif
