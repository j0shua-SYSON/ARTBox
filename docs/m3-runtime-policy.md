# ART runtime policy prerequisites

The first runtime build omits the ART compiler library. Its `jit_create()`
definition matches the pinned AOSP declaration and terminates with exit 126
and a diagnostic if called. It never returns a null compiler object: upstream
`Jit::Create()` immediately uses the returned interface.

This is an invariant check for the diagnostic bring-up process. It does not
make runtime startup safe by itself. Startup must select `-Xint`, disable
compilation and profile saving, and reject requests for new executable memory.
ART creates its code cache before calling the compiler factory. The current
standalone fixture does not start ART or verify that earlier allocation path.

Stack unwinding retains the upstream C++ demangler. An explicit
`ARTBOX_NO_RUST_DEMANGLE` option leaves Rust names unchanged, avoiding a Rust
runtime dependency for optional stack-trace labels. Java execution and frame
unwinding are unaffected by this source edit; their correctness still requires
separate runtime tests. The original source remains available for comparison.

Run the native behavioral fixture with a C++20 GCC/Clang toolchain:

```text
python -B scripts/test_art_runtime_policy.py --cxx c++
```

The command uses the shared configurable environment and source cache. Each
attempt preserves its binaries, build logs, source hashes, notices and results
under `build/m3/runtime-policy/` by default. `--build-dir` selects another output
directory. The compiler can also be selected through `CXX`.

Eight name cases cover empty, plain, malformed, C++ and Rust symbols. The
unchanged source invokes a test-only Rust callback twice and fails the desired
no-Rust contract. The adapted source passes without defining or importing that
callback. A separate child process calls the forbidden JIT factory and must
exit 126 without returning to its caller.

Windows native function tests pass locally. CI runs the same cases on native
macOS/Linux ARM64. The Mac job also builds and signs an iOS 15 framework with
the same functions. Those CI outcomes are pending for this change; building
the framework does not establish iPhone execution. These tests are not M3
acceptance, ART startup, a Java benchmark or a complete code-generation audit.
