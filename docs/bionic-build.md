# Bionic source build

`python scripts/build_bionic.py` compiles the pinned Android 15 sources selected
in `third_party/bionic/m2-objects.json`. The current selection includes 23
pthread translation units, three libc initialization units, errno accessors,
futex/clone wrappers, open/stat wrappers and the AArch64 TLS setter. It also
builds C++ initialization/destruction support, ELF TLS helpers, auxv/vDSO access,
tracing, fd ownership/tracking, semaphores and signal wrappers. It produces
167 Bionic objects (including generated syscall assembly), 23 Scudo/GWP-ASan
objects and 14 AOSP Arm memory/string objects
and combines them with a relocatable link. The default
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
specifies exact, hash-checked edits to six upstream files, written into an
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
  uses the host dispatch endpoint described below; signal translation is not implemented.
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

At implementation `9d22426`, `python scripts/test_bionic.py` compiled both profiles from the same 96 selected/generated
sources. They share 894 global definitions; the exact differences are four weak
Scudo template outlines and GWP-ASan's replaced eight-byte TLS variable. The
check rejects any other difference, any strong-definition difference or an
unexpected ELF TLS object. The upstream control has 189 `TPIDR_EL0` reads,
one write, one x18 reference and 221 `svc` instructions. The
native profile contains none of those instructions, no x27/x28 references,
no `svc`, no unknown disassemblies and no outlined atomic imports. It retains
both TLS bridge imports, the raw-syscall endpoint and stack-check failure calls.
The native object has 94 failure-branch relocations and 218 guard-address
relocations. This checks actual code references even after the failure handler
is defined. The partial object still has 72 unresolved dependencies; no missing function is replaced
with a placeholder.

The remaining libc/loader components and special clone/teardown assembly are absent
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
Linux-only test backend with the fixed-width raw interface; the
original header enters Linux directly. This is a source-adaptation test, not a
Bionic execution or Darwin signal result. It uses Clang, matching Bionic's
compiler family, and checks that the original path still contains a kernel-entry
instruction while the adapted path does not. The first GCC-built upstream
control timed out: a local reproduction showed GCC discarding this non-volatile
assembly because its output is unused, consistent with [GCC's documented
optimization](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html#Volatile).
The check rejects a lost control instead of treating it as a successful adaptation.

At implementation commit `cad1f910c525977e12cf6daf184c63bf3eadfdee`, the
[host and Linux ARM64 run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34749696847)
passed both 48-unit profiles and both signal paths. The Linux compiler was
Clang 18.1.3; it retained three kernel-entry instructions in the original test
and zero in the adapted test. Each delivered 1,024 signals across eight threads
with zero failures. The adapted endpoint received 1,032 calls, including the
eight invalid-signal checks.

The [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34749696848)
also passed. Its downloaded transport IPA at `artifacts/m2-cad1f91/ios/ARTBox.ipa`
has SHA-256 `a433a2c0213e2dd80fc862b49b1a5e66752a8cea7c6a81448eb454fa46d748bd`.
The app and both M1 frameworks retain arm64 iOS 15 metadata and match their
signed manifests. The tested PR merge is `34db396a409cf03dce91a1f9e595f922d801ab50`,
whose parent includes the implementation commit. Bionic object, notice and
exported-header hashes were checked after download. The IPA still contains
the M1 runtime; these results do not close M2.

## Generated syscall boundary

The pinned AOSP `gensyscalls.py` and `SYSCALLS.TXT` produce all 216 ARM64 stubs
and 13 aliases. Their hashes and the generic `syscall.S` hash are recorded in
`third_party/bionic/syscalls.json`. ARTBox adapts the generated source before
assembly: save FP/LR with unwind directives, shift up to six arguments into the
fixed seven-word host endpoint, zero unused words, then restore the frame. The
original Bionic errno tail remains: only raw results from -4095 through -1 become
-1 with errno set. The generic Android variadic entry already has the seven
register arguments in the required order; no variadic call crosses into Darwin.
Assembly retains Soong's `-D__ASSEMBLY__` and Bionic's `-D_LIBC=1` settings.

The syscall oracle links the exact NDK-assembled stubs, generic entry and real
Bionic `__set_errno_internal` into prefixed test objects. A native Linux runner
provides separate test errno storage and a capture backend. Every entry/alias
gets nine boundary return values on four threads, with distinct 64-bit argument
words and explicit x18, x19-x29 and stack/frame preservation checks. Only five
selected smoke cases reach Linux: PID, UID, bad-fd write, zero-length mmap and
an unknown syscall. Both original and adapted entries run those cases. The
runtime check passes on native Linux ARM64; it does not prove complete Bionic
startup, Darwin translations or dynamic packaging. A generated entry is not an
implemented syscall; unsupported operations still require an explicit runtime
error. The additional frame/call/argument moves have not been timed.

At implementation `b2bc96678744b0282188fb391f6d1239fcc0d722`, the
[host and Linux run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34750412923)
passed all 230 entry points, 8,280 capture cases and five smoke cases for each
profile with zero failures. Downloaded NDK object/header hashes match the
Linux report. The [iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34750412917)
passed its M1 regression. Its IPA at `artifacts/m2-b2bc966/ios/ARTBox.ipa` has
SHA-256 `536f3e3dbd35a3ef65ef175f3b3d06c8a8b4332d2d8ee6521930422f45c89c1d`.
The tested merge `e2c30ab52b5897da79347f06e914eb1938ce7886` includes that
implementation as a parent. Both downloaded partial objects, notices and
exported headers match their reports. Bionic is not yet embedded in this IPA.

The precompiled host endpoint now selects an immutable per-thread dispatch
binding, forwards all seven words and preserves host errno for returning calls.
It leaves raw results unchanged so the original Bionic tail owns guest errno.
New threads start unbound and receive -ENOSYS until the runtime binds a handler.
The caller owns the binding/context lifetime and restores prior bindings after
nested entry or non-local exit. A host test covers 12 threads and 1,000 nested
calls per thread, plus the five M1 translations and exit cleanup through the
exported endpoint. This provides the transport; full Bionic TLS and thread
startup still need integration. The extra call and host TLS lookup are untimed.

## Allocator dependency build

`third_party/bionic/components.json` selects the real Scudo and GWP-ASan source
groups from their matching AOSP tag. It records compiler flags and notice hashes.
Scudo retains Android's custom size classes, shared TSD registry and Bionic
wrappers, `-O3`, CRC support and its upstream stack-protector exception. Both
profiles disable TBI/MTE through Scudo's existing configuration switches; the
runtime must advertise CPU capabilities accurately. GWP-ASan retains its sampled
guarded allocator and crash-handler implementation. The Bionic dynamic malloc,
heapprofd and limit wrappers are compiled rather than replaced.

Both components use Soong's `-DANDROID` as well as the NDK's `__ANDROID__` macro.
The initial compile probe omitted the former and selected an empty POSIX mapping
name helper; retaining the upstream Android define fixed that error with warnings
still treated as errors. Component-specific compiler flags are recorded per object.

GWP-ASan's default initial-exec TLS reads `TPIDR_EL0`, even when Bionic's explicit
TLS getter is adapted. Its supported platform-header hook now returns an actual
`ThreadLocalPackedVariables` object from `TLS_SLOT_NATIVE_BRIDGE_GUEST_STATE`.
The runtime must initialize aligned storage with the upstream constructor and
bind it before allocator entry; it cannot allocate recursively inside the getter.
The threaded Linux fixture compiles the original state/getter or the platform
hook with the same NDK inputs. It checks the constructor's sentinel, all bit
fields, errno preservation and independent state over 8,192 exchanges. Both
profiles pass in native Linux CI. This is a TLS adaptation test, not allocator startup.

The instruction-gated native partial object at `9d22426` contains 38,505 decoded instructions
with zero checked kernel entries, thread-pointer or reserved-register accesses.
Its 897 global definitions differ from the control's 896 only by the exact weak
outlines/TLS object listed in the allocator manifest. The extra calls change
Clang's inlining decisions; those internal C++ outlines are not missing libc
exports. Full dynamic packaging, malloc execution, mapping semantics and the
remaining 72 dependencies still need integration.

Implementation `9d22426eb134f90daf18ee7709a2db05eee05695` passes
[host and Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34751449577)
and the [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34751449507).
Each allocator TLS profile completed all 8,192 exchanges on eight threads with
zero failures; the existing signal, syscall and M1 native checks also passed.
Downloaded partial objects, caller objects, exported headers and all four
component notices match their reports. The tested merge
`db2ec90937a48ccff7f9957a9296035ef7c654d5` includes that implementation parent.
The M1 IPA at `artifacts/m2-9d22426/ios/ARTBox.ipa` has SHA-256
`e979fbc7636ddc7c80a009923980679a553873860c6c8f1afbbf7d60cfe5a323`.
It retains the verified arm64 iOS 15 app/framework manifests; it contains no
Bionic allocator runtime and has not been executed on a physical device.

## Stdio and baseline strings

The selection at `3f69405` has 204 units. It adds the actual Bionic stdio, OpenBSD
stdio/gdtoa dependencies, locale/ICU dispatch and source providers required by
the existing libc initialization and malloc wrappers. The source manifest
records each OpenBSD group's compatibility include, warning settings and
upstream larger-stack exception. The common warning settings also retain
Soong's `-Wno-c99-designator`. Android's FILE layout, varargs handling and
long-double conversion stay in Android-compiled code; no host printf adapter
is substituted. This remains a partial source build with unresolved imports.

`components.json` now includes the minimal pinned AOSP Arm string library.
Bionic's unchanged static dispatcher binds its 15 public entry points to the
baseline implementations; see ADR 0015. The build produces `strings-test.o`
from the exact production objects with test-only symbol prefixes, plus an
original NDK-compiled scalar oracle. It rejects unresolved test imports and
forbidden instructions. Linux and the signed macOS wrapper must each complete
all 35,908 guarded-page cases. Compile success alone is not a runtime result.

Implementation `3f69405083b12e386bd7ea9d5b9fb9eb9b9ac65c` passes
[host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34753272777)
and the [iOS regression](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34753272781).
Both Linux profiles complete all 35,908 cases with 4 KiB pages. The signed
macOS wrapper completes the same cases with 16 KiB pages, plus the existing
100 syscall iterations. Source objects, notices, the expanded iOS framework
and M1 IPA were downloaded and hash/layout verified. The native partial build
has 71,228 decoded instructions, 1,263 global definitions, 198 stack-failure
branches and 661 guard-address relocations. It has 33 unresolved imports,
including 30 strong imports; the existing five weak profile differences remain
exact. This is substantial source closure, not complete Bionic initialization.

## Property and CRT dependencies

The current selection compiles 221 units: 183 Bionic source/generated units,
23 allocator units, 14 Arm string units and one property-info parser. It uses
the real shared-library CRT for pthread-atfork registration and finalization,
wide-string conversion dependencies, Bionic's six system-property implementation
units and public API, and their mkdir/xattr/string-duplication providers.
The FreeBSD sources use their upstream compatibility include and warning flags;
the ARM64 CRT does not enable the legacy ARM32 workaround.

Only five hash-pinned files are fetched for property-info from the already
reviewed system/core revision. The complete notice and original source/header
notices accompany object artifacts. The property parser and Bionic startup will
use the virtual filesystem; no host properties or success-only placeholder is
substituted. Actual property initialization still requires runtime integration.

Local NDK comparison passes with 1,378 shared global definitions and the same
five allowed weak differences. The native build has 75,560 decoded instructions,
221 stack-failure branches and 709 guard-address relocations. All checked
syscall/thread-pointer/reserved-register instruction counts remain zero. Its
21 unresolved dependencies include 18 strong imports: four binary128 compiler
helpers, loader APIs, native TLS/syscall endpoints and thread/process boundaries.
This source selection passes [host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34755550388)
and the [iOS regression](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34755550375)
at `aaeece1`; actual property startup remains pending.

Two reviewed target compiler-rt members from the pinned NDK now supply binary128
comparison and multiplication, removing those four strong imports. This keeps
Android's 128-bit long double entirely in its own compiled ABI. Both profiles
link identical hash-checked members and retain the complete LLVM toolchain notice.
The original NDK test caller passes only an integer result across the Apple
boundary: 123 exact-bit products, ordered comparisons, signed-zero and NaN cases.
CI executes the identical caller/helpers on native Linux and in the signed
macOS wrapper; iOS linking/signing preserves the same bytes. Local compilation
and instruction checks pass, as do native arithmetic tests at `726d758`.

With these helpers the local native object has 75,947 decoded instructions,
1,387 global definitions shared with the control and 17 unresolved dependencies
(14 strong). The 221 compiled source units are separate from the two prebuilt
NDK arithmetic members. This is still not complete Bionic startup.

The subsequent [startup integration](bionic-startup.md) links this selection
into a shared libc and executes its actual TLS initialization, constructors and
allocator in a signed macOS wrapper. Its controlled load group supplies the
real shared-global object and native interfaces; unimplemented thread/namespace
imports fail the test if invoked. General loader coverage and M2 acceptance are
still incomplete. The property initializer executes but cannot load its backing
files until the virtual filesystem is implemented.

Implementation `726d75825b858b9e648bcf23ee1f2d43717f4ced` passes
[host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34755905118)
and the [iOS regression](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34755905119).
The 123 arithmetic cases take 8 microseconds in the signed Mac wrapper and
2.104 microseconds on the native Linux runner, including the test caller.
These single samples are correctness-fixture timings, not a platform performance
comparison. The exact CI NDK test-object SHA-256 is
`cbef4a00ab63b5d07aea0efeae1689a0087c1e6b75d4e3ae5787e9efdcfdcd58`.
Both hosts use the two pinned production members unchanged. Downloaded partial
objects, component notices and signed iOS framework layout match their reports.
