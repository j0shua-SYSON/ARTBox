// Copyright (c) ARTBox contributors. MIT license.
#include "jit_create.h"
#include <cstdio>
#include <cstdlib>

namespace art::jit {

JitCompilerInterface* jit_create() {
  // Startup must disable compilation and profiling before any code-cache request.
  // Returning would let ART proceed with an invalid compiler object.
  std::fputs("ARTBox: runtime JIT compiler creation is forbidden\n", stderr);
  std::fflush(stderr);
  std::_Exit(126);
}

}  // namespace art::jit
