# Bionic source build

`python scripts/build_bionic.py` compiles the pinned Android 15 sources selected
in `third_party/bionic/m2-objects.json`. The current selection includes 23
pthread translation units, three libc initialization units, errno accessors,
futex/clone wrappers, open/stat wrappers and the AArch64 TLS setter. It also
builds C++ initialization/destruction support, ELF TLS helpers, auxv/vDSO access,
tracing, fd ownership/tracking, semaphores and signal wrappers. It produces
48 AArch64 objects and combines them with a relocatable link. The default
`native` profile applies the source adaptations below; `--profile upstream`
builds a control with the original source. Neither produces `libc.so` or packages
Android code into the app. `--ndk-root`, `--build-dir` and `--jobs` are configurable;
source/cache locations follow the shared Python environment settings.

Bionic's `libc_defaults` uses
`stl: "none"`, so the compiler uses `-nostdinc++` and Bionic's minimal
`libstdc++/include` instead of NDK libc++. The pinned [Soong compiler
configuration](https://android.googlesource.com/platform/build/soong/+/refs/tags/android-15.0.0_r1/cc/config/global.go)
selects `gnu++20` and suppresses `-Wnon-c-typedef-for-linkage`,
`-Wmissing-field-initializers` and `-Wvla-cxx-extension`. These settings are
needed with the pinned NDK compiler: C++17 rejects `constinit` in libc
initialization, and omitting the existing Soong warning exception breaks
`-Werror` on Bionic's allocator header. ARTBox retains warnings as errors.
The source manifest also preserves the upstream bootstrap exceptions:
`__libc_init_main_thread.cpp`, `__set_tls.c`, `__stack_chk_fail.cpp` and
`getauxval.cpp` use `-fno-stack-protector` and
`-ffreestanding`; `libc_init_dynamic.cpp` uses `-fno-stack-protector`. They run
before TLS/stack guards or string IFUNCs are ready. Other units retain strong
stack protection. C sources use Bionic's `gnu99` setting.

The compiler reserves x18, x27 and x28 and disables emulated ELF TLS. These flags
alone do not adapt handwritten assembly or TLS access. `native-boundary.json`
specifies exact, hash-checked edits to five upstream files, written into an
overlay under the build directory. Original source and notices stay intact.

- `__get_tls()` calls the precompiled `artbox_bionic_get_tls` endpoint.
- The hidden Bionic `__set_tls` definition calls `artbox_bionic_set_tls`; it
  never writes the host thread-pointer register.
- Android shadow-call-stack allocation/register setup and cleanup are omitted
  for this profile, because x18 belongs to the Apple ABI. Guest code must be
  compiled without Android shadow-call-stack instrumentation.
- `inline_raise` submits its signal through a fixed seven-word
  `artbox_bionic_syscall` endpoint instead of inline `svc`. This raw endpoint
  must return negative Linux error numbers without modifying guest errno. It
  remains an unresolved runtime dependency; signal translation is not implemented.
- Compiler stack checks use Bionic's global guard. Atomic operations use the
  baseline Armv8-A instruction path with `-mno-outline-atomics`, so they do not
  depend on Android's CPU-feature-detecting atomic helpers.

These are source/build adaptations made before signing. Guest TLS access now
costs a C call plus host TLS lookup; atomic and shadow-stack choices carry
performance/security tradeoffs recorded in ADR 0011. No timings are claimed.

The host endpoints store a borrowed Bionic slot pointer in the host compiler's
ordinary TLS. The loader must bind initialized TLS before entering Bionic,
restore any prior binding on exit, and own its allocation and lifetime. The
host test checks 12 real threads with 10,000 nested bindings each, preserving
their separate host TLS values. It does not yet execute Bionic's code.

## Current evidence and next boundaries

`python scripts/test_bionic.py` compiles both profiles from the same 48 selected
sources and verifies that they retain the same 282 global definitions. The
upstream control has 108 `TPIDR_EL0` reads, one write, one x18 reference and
two inline `svc` instructions in the fd-ownership diagnostic path. The
native profile contains none of those instructions, no x27/x28 references,
no `svc`, no unknown disassemblies and no outlined atomic imports. It retains
both TLS bridge imports, the raw-syscall endpoint and stack-check failure calls.
The native object has 58 failure-branch relocations and 140 guard-address
relocations. This checks actual code references even after the failure handler
is defined. The partial object still has 101 unresolved dependencies; no missing function is replaced
with a placeholder.

The remaining libc/allocator/loader components and syscall assembly are absent
from this source selection. Their code needs the same checks when introduced.
TLS block creation, module TLS layout, source dependency closure and native
execution through signed dynamic packaging remain required. The instruction
gate covers the listed classes; it is not a complete ABI compatibility proof.

`m2-bionic-upstream.json`, `m2-bionic-native.json` and `m2-bionic-profile-check.json`
record the source pin, compiler/per-file flags, source and overlay hashes,
global definitions, object hashes, unresolved symbols and instruction counts.
The tracing helper also uses two unmodified, separately pinned libcutils headers;
the rest of system/core is not fetched. CI keeps both partial objects with the
hash-verified complete Bionic libc and libcutils notices.
Those are compile and inspection results, not M2's multithreaded runtime acceptance.

The native Linux oracle compiles both exported versions of the actual
`bionic_inline_raise.h` against the system libc. It requires 1,024 signal
deliveries across eight threads, checking receiver thread, signal, sender,
payload and unchanged errno after invalid signals. The adapted header calls a
Linux-only test backend with the proposed fixed-width raw interface; the
original header enters Linux directly. This is a source-adaptation test, not a
Bionic execution or Darwin signal result. The current CI result must be checked
before treating this new oracle as verified. It uses Clang, matching Bionic's
compiler family, and checks that the original path still contains a kernel-entry
instruction while the adapted path does not. The first GCC-built upstream
control timed out: a local reproduction showed GCC discarding this non-volatile
assembly because its output is unused, consistent with [GCC's documented
optimization](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html#Volatile).
The check rejects a lost control instead of treating it as a successful adaptation.
