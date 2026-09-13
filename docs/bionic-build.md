# Bionic source build

`python scripts/build_bionic.py` compiles the pinned Android 15 sources selected
in `third_party/bionic/m2-objects.json`. The current selection includes 23
pthread translation units, three libc initialization units, errno accessors,
futex/clone wrappers, open/stat wrappers and the AArch64 TLS setter. It produces
34 AArch64 objects and combines them with a relocatable link. The default
`native` profile applies the source adaptations below; `--profile upstream`
builds a control with the original source. Neither produces `libc.so` or packages
Android code into the app. `--ndk-root`, `--build-dir` and `--jobs` are configurable;
source/cache locations follow the shared Python environment settings.

Bionic's `libc_defaults` uses
`stl: "none"`, so the compiler uses `-nostdinc++` and Bionic's minimal
`libstdc++/include` instead of NDK libc++. The pinned [Soong compiler
configuration](https://android.googlesource.com/platform/build/soong/+/refs/tags/android-15.0.0_r1/cc/config/global.go)
selects `gnu++20` and suppresses `-Wnon-c-typedef-for-linkage`. Both settings are
needed with the pinned NDK compiler: C++17 rejects `constinit` in libc
initialization, and omitting the existing Soong warning exception breaks
`-Werror` on Bionic's allocator header. ARTBox retains warnings as errors.
The source manifest also preserves the upstream bootstrap exceptions:
`__libc_init_main_thread.cpp` and `__set_tls.c` use `-fno-stack-protector` and
`-ffreestanding`; `libc_init_dynamic.cpp` uses `-fno-stack-protector`. They run
before TLS/stack guards or string IFUNCs are ready. Other units retain strong
stack protection. C sources use Bionic's `gnu99` setting.

The compiler reserves x18, x27 and x28 and disables emulated ELF TLS. These flags
alone do not adapt handwritten assembly or TLS access. `native-boundary.json`
specifies exact, hash-checked edits to four upstream files, written into an
overlay under the build directory. Original source and notices stay intact.

- `__get_tls()` calls the precompiled `artbox_bionic_get_tls` endpoint.
- The hidden Bionic `__set_tls` definition calls `artbox_bionic_set_tls`; it
  never writes the host thread-pointer register.
- Android shadow-call-stack allocation/register setup and cleanup are omitted
  for this profile, because x18 belongs to the Apple ABI. Guest code must be
  compiled without Android shadow-call-stack instrumentation.
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

`python scripts/test_bionic.py` compiles both profiles from the same 34 selected
sources and verifies that they retain the same 171 global definitions. The
upstream control has 57 `TPIDR_EL0` reads, one write and one x18 reference. The
native profile contains none of those instructions, no x27/x28 references,
no `svc`, no unknown disassemblies and no outlined atomic imports. It retains
both TLS bridge imports and stack-check failure calls. The native partial
object still has 93 unresolved dependencies; no missing function is replaced
with a placeholder.

The remaining libc/allocator/loader components and syscall assembly are absent
from this source selection. Their code needs the same checks when introduced.
TLS block creation, module TLS layout, source dependency closure and native
execution through signed dynamic packaging remain required. The instruction
gate covers the listed classes; it is not a complete ABI compatibility proof.

`m2-bionic-upstream.json`, `m2-bionic-native.json` and `m2-bionic-profile-check.json`
record the source pin, compiler/per-file flags, source and overlay hashes,
global definitions, object hashes, unresolved symbols and instruction counts.
CI keeps both partial objects with the hash-verified complete Bionic libc notice.
Those are compile and inspection results, not M2's multithreaded runtime acceptance.
