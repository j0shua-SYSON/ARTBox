# Third-party provenance

## M4 Binder references and UAPI boundary

`binder-references` in `third_party/sources.json` selects 15 reference files from
`aosp-mirror-neo/platform_frameworks_native`, tag `android-15.0.0_r1`, commit
`f7274fca5e36082674740bc6c976f73c4578d009`. Each file records its size, SHA-256
and Git blob identity. The selection includes root NOTICE/license metadata,
servicemanager source/build/tests, libbinder process/thread source/build and
two service AIDL files. The selected source files carry Apache-2.0 notices;
the parent Android.bp lists additional licenses covering unrelated files.
Preserve the complete NOTICE; do not relicense AOSP code as MIT. These files
are reference inputs only and are not compiled or included in the IPA yet.

The original MIT Binder wire parser and tests copy no kernel implementation.
Linux v6.12 `drivers/android/binder.c` (GPL-2.0) is consulted only for ABI behavior
and is not vendored or shipped. The existing pinned NDK r28c supplies its UAPI
header for a compile-time comparison and data fixture; its existing bundled
license terms continue to apply. No additional runtime library is imported.
See [the Binder contract](docs/binder.md).

The eventfd oracle and shared counter fixture are original MIT userspace code.
Linux v6.17 [`fs/eventfd.c`](https://github.com/torvalds/linux/blob/v6.17/fs/eventfd.c)
(GPL-2.0-only) was reviewed for transfer ordering and lifetime semantics only;
no implementation is copied, vendored, linked or included in an Apple bundle.
Native tests call the CI host's existing Linux syscalls and use its system UAPI
headers. The Android ARM64 compile check uses the already reviewed NDK headers.

The monotonic timerfd fixture and native wait tests are also original MIT code.
Linux v6.17 [`fs/timerfd.c`](https://github.com/torvalds/linux/blob/v6.17/fs/timerfd.c)
(GPL-2.0) is a behavior-only reference for expiration accounting, copyout and
set/get ordering. No kernel code is imported or shipped. The oracle executes
normal timerfd/epoll calls on existing native Linux CI hosts.

The native Linux reference fixture additionally uses Ubuntu's unmodified
`linux-modules-extra-6.17.0-1022-azure`, version `6.17.0-1022.22`, matching CI's
running kernel. `third_party/binder/kernel-reference.json` records the official
package URL, package/module/copyright sizes and SHA-256 hashes. Its reviewed
copyright declares GPL-2.0. The script extracts only the Binder module and
notice into its configured cache, verifies module vermagic/license, and uses
the existing host kernel in a private binderfs mount on a disposable CI host.
This module has no exit hook and remains loaded until the host is discarded;
the private mount and module references must be cleaned up. No package is installed;
no kernel is booted. The module/package are not uploaded as project artifacts,
linked into ARTBox, copied into an Apple bundle or relicensed. ARTBox's original
MIT ioctl fixture runs as an independent userspace program. Kernel reference
coverage is reported separately from comparisons with the ARTBox driver.

`binder-aidl` adds only the five unchanged `libs/binder/aidl/android/os` inputs
listed by libbinder's Android.bp, plus that build file and the full NOTICE, at
the same Android 15 frameworks/native pin. They remain Apache-2.0. The generated
C++ interfaces are build inputs for real libbinder/servicemanager; generation
alone does not run either component or include them in an IPA.

`third_party/binder/aidl-tools.json` pins AOSP build-tools tag
`android-15.0.0_r1`, commit `d91d878ca61f179f08161888beab574aa38bcddd`.
Only the selected host's `aidl` and companion C++ library are downloaded, along
with build provenance. The macOS files contain native arm64 and x86_64 slices;
the Linux profile uses native x86_64 and its existing system libc. There is no
host binary for Windows in this selection; cross-host preparation verifies bytes
without executing them. The build manifest identifies the compiler source as
`system/tools/aidl` commit `1c5712d44317bdbfaeab915c2666f2b662d6b528`.

The compiler's AOSP source, libbase and liblog notices are Apache-2.0. Its pinned
build manifest also supplies exact revisions for googletest (BSD-3-Clause), fmt
(MIT), libc++/libc++abi/compiler-rt (their retained MIT/NCSA notices), Flex (BSD
terms), and Bison's GPL-3.0 text and parser-skeleton exception. Preserve these
complete notices and the skeleton's exception text; do not label host tooling
as ARTBox MIT code. Each selected notice is checked against the build manifest,
SHA-256 and Git blob. No host compiler/library binary is uploaded with generated
bindings or embedded in ARTBox. Generated artifacts retain the original AIDL,
AOSP NOTICE, tool source provenance and license records. A future distribution
of these host binaries needs its own complete redistribution review.

`third_party/binder/native-sources.json` additionally pins 144 libbinder source,
header and build/notice files at the same frameworks/native revision, 60 libutils
Binder support files and 43 system headers/build/notice files from system/core
`fc7bc8c4bfb4c6095e34ea784509dae56f25b486`, four APEX API/reference files from
system/apex `896cdde58b67ddbf37ffdfe06f5b2567ef770f70`, and Soong's build-flag
reference at `3fe78dc9b32f1ac3f7eb279ae9376d7037b18452`. All use the named tag
`android-15.0.0_r1`. This selection compiles AOSP libbinder, eight
libutils Binder support units and `Looper.cpp`/`Timers.cpp` into Android ARM64
archives; it is not embedded in the IPA yet. Looper's file header attributes the
Android Open Source Project; the package's full Apache-2.0 NOTICE applies.
Timers also carries an explicit Apache-2.0 declaration. Native Linux Looper
tests reuse the existing Apache-2.0 AOSP host liblog sources. The shared test
caller and its build script are original MIT code.

For the signed runtime, ARTBox adapts compiler-generated TLS assembly from the
Apache-2.0 `libs/binder/BufferedTextOutput.cpp`: one host thread-pointer read uses
the existing absolute-TLSDESC convention. The original C++ source remains intact.
The Binder build graph pins the source hash and TLS access inventory; artifacts
retain the original/adapted assembly and objects, build recipe and complete
frameworks/native NOTICE. This build-time modification is described in ADR 0119.

`binder-platform` adds 16 files at the same system/core commit and Android 15 tag:
nine unchanged Apache-2.0 units for libutils threading/time/property callbacks,
libcutils native handles, multiuser IDs, properties, ashmem and tracing, and
libvndksupport loading; the remaining files are the exact supporting header,
trace include, build descriptions and complete libutils/libcutils notices.
Every selected file records its size, SHA-256 and Git blob identity. These
compile into three additional support archives, not an executing Android service.
Compiler-profile differences and unimplemented runtime dependencies are recorded
in [the Binder build description](docs/binder-build.md) and ADR 0120.

The Binder libc dependency selection adds Bionic's unchanged `libc/bionic/time.cpp`
and `libc/upstream-openbsd/lib/libc/gen/fnmatch.c`, at the existing Bionic commit
`361ba86734fb2821a6adcfdf775db8abd04e0de0` (`android-15.0.0_r1`). The former has
Bionic's BSD-style notice; the latter retains VMware's BSD-3-Clause and the
OpenBSD authors' ISC notices. Their exact source hashes and full `libc/NOTICE`
are pinned in `third_party/bionic/binder-libc.json` and retained in the native
reference source archive. The signed Bionic framework already carries this
complete libc notice. The independent Linux reference additionally links the
pinned NDK's static libc and retains both NDK distribution and toolchain notices.
That Linux executable is test-only and is not an iOS bundle input. The shared
caller, negative controls and build scripts are original MIT code.

The signed Looper diagnostic also selects Bionic's unchanged
`libc/bionic/eventfd.cpp` from the existing `android-15.0.0_r1` source pin. It
retains its AOSP BSD notice under the complete Bionic NOTICE already included
with libc. The file supplies public eventfd wrappers and fd tracking; syscall
translation stays in the original MIT VFS. The new Looper framework retains
libutils, supporting source/header, NDK and ARTBox license notices. Its C++ and
logging imports resolve from the existing signed ART dependency image; no
replacement logging or C++ runtime implementation is introduced.

The implementations and APEX/Soong reference files retain Apache-2.0 notices.
Keep the complete frameworks/native NOTICE, libutils NOTICE and libcutils NOTICE.
The small forwarding and generated graphics headers inherit their package's
Apache-2.0 declaration. `vm_sockets.h` retains its original generated-UAPI
declaration and uses the NDK's real Linux UAPI header in Bionic builds; no kernel
implementation is compiled. APEX/Soong have per-file license declarations and
use the complete Apache text retained with libutils. Existing libbase/liblog
header pins and their notices are reused. Preserve the full reviewed NDK NOTICE
for compiled-in header definitions. Source artifacts include all selected
originals, generated bindings, build recipes and notices. Original build scripts
and artifact-integrity tests are MIT; AOSP files are not relicensed.

## M3 ART math dependency

The [math subset](docs/m3-math.md) selects 28 unchanged source units from
Bionic `android-15.0.0_r1`, commit `361ba86734fb2821a6adcfdf775db8abd04e0de0`,
and seven unchanged math units from AOSP arm-optimized-routines at that tag,
commit `514df029da8aa4146726f6a020ddc69cf34a007d`. The latter has a separate
`arm-math` exact-file pin in `third_party/sources.json`; the Bionic selection
and full libm notice hash are in `third_party/bionic/art-math.json`.

The BSD math sources retain Sun permission notices and BSD terms, including
attribution and non-endorsement where present. AOSP compatibility headers and
builtins carry their Apache-2.0 or BSD notices. Selected ARM sources offer
MIT or Apache-2.0 WITH LLVM-exception; this import uses the MIT option and
preserves the complete original LICENSE. Internal binary128 arithmetic links
from the already pinned NDK r28c compiler-rt archive under its bundled notice.
No upstream source is modified or relicensed. Corresponding-source archives
retain original selected source/header texts and notices; every signed library
and client framework includes complete Bionic libm/libc, ARM, NDK and ARTBox
license files. This dependency is currently a temporary M3 test artifact,
not part of the integrated M2 IPA.

The M3 libc dependency expansion adds 50 unchanged units from the existing
Bionic pin. Their paths, SHA-256 hashes and per-source AOSP flags are recorded
in `third_party/bionic/m2-objects.json`. The selection includes Bionic wrappers,
BSD libc routines and Bionic's time-zone sources, retaining their original BSD,
permissive and public-domain notices. The complete existing Bionic libc NOTICE
continues to accompany every Bionic framework; the code is not relicensed.
No additional repository is imported for this expansion. See
[the dependency contract](docs/m3-bionic-dependencies.md) for execution scope.

M0 vendors no third-party runtime source, binaries, Android images, or APKs.
ARTBox's MIT license covers its original code, not future dependencies.

## Reviewed references (2026-09-13)

| Reference | License reviewed / boundary | Use here |
| --- | --- | --- |
| [S5LBox](https://github.com/j0shua-SYSON/S5LBox/tree/6f203ba550b49afadee008c7eb55373a838eed33) | MIT, copyright 2026 j0shua-SYSON | Build/signing pattern studied; original M0 implementation. No source copied. |
| [Waydroid](https://github.com/waydroid/waydroid) | GPL-3.0 repository declaration | Architecture reference only; no copied code. |
| [Anbox](https://github.com/anbox/anbox) | GPL-3.0 repository declaration | Architecture reference only; no copied code. |
| [Box64](https://github.com/ptitSeb/box64) | MIT repository declaration | Loader/translation reference only; no emulator included. |
| [FEX](https://github.com/FEX-Emu/FEX) | MIT repository declaration; dependencies separately licensed | Reference only; no code included. |
| [Darling](https://github.com/darlinghq/darling) | GPL-3.0 repository declaration; submodules vary | Userspace server reference only; no copied code. |
| [UTM](https://github.com/utmapp/UTM) | Apache-2.0 top level, includes (L)GPL components | Signing/execution contrast only; no code included. |
| [AOSP](https://source.android.com/docs/setup/contribute/licenses) | Component-specific; not uniformly Apache-2.0 | Reference only at M0. |
| [ANGLE](https://chromium.googlesource.com/angle/angle/+/main/LICENSE) | BSD-style primary license, third-party notices also required | Metal backend documentation studied; deferred to graphics milestone. |

Bionic's inspected `libc/private/bionic_tls.h` at `android-15.0.0_r1` carries
a BSD-style notice. The inspected libbinder `IPCThreadState.cpp` and
servicemanager `main.cpp` at that tag use Apache-2.0. Their later imports are
recorded in the M4 section above.
Do not replace these licenses with ARTBox's MIT license. AOSP origin does not
mean that every file is Apache-2.0; review libcore/OpenJDK and transitive
dependencies before the ART milestone.

## Import rule

Before each import, record upstream URL, named tag, resolved commit, archive
SHA-256 (if applicable), imported paths, license texts, notices, changes, and
whether the code is shipped or only used to build. Keep required notices with
the artifact. Fetch only what the active milestone needs. Never import GMS,
Google Play Services, vendor blobs, or a reference project's CPU emulator.

Preinstalled CMake, Ninja, GCC/Clang, Xcode, Git, GitHub CLI, and hosted Actions
are build tools, not vendored dependencies. No package manager installs them.

## M1 build tools

The original `fixtures/hello/hello.S` is assembled with Android NDK **r28c**
(`28.2.13676358`, Clang 19.0.1), using `-nostdlib` and a project linker script.
It includes no Bionic or NDK runtime objects. NDK archives remain build tools,
outside Git and the shipped app; their bundled notices govern those tools.
The pinned release is [android/ndk r28c](https://github.com/android/ndk/releases/tag/r28c).

Apple's [Mach-O format header](https://github.com/apple-oss-distributions/cctools/blob/main/include/mach-o/loader.h)
was consulted for container constants and layouts. Its APSL-2.0 header is not
vendored or copied into ARTBox; the Python encoder is original implementation.

## M2 source pin

Bionic is pinned to [android-15.0.0_r1](https://github.com/aosp-mirror/platform_bionic/tree/361ba86734fb2821a6adcfdf775db8abd04e0de0)
at commit `361ba86734fb2821a6adcfdf775db8abd04e0de0`, from AOSP's GitHub mirror.
`third_party/sources.json` records the retrieved archive and libc notice hashes.
The source includes BSD and other permissive notices; libdl has an Apache-2.0
notice. Preserve component notices and individual source headers. Bionic is not
relicensed as MIT. The M1-only app omits Bionic; the integrated M2 app embeds
the four tested libraries and complete notices described below.

The source fetch step omits editor configuration, header-versioner tooling and
unused netfilter kernel headers listed in the lock file. Netfilter's
case-colliding filenames cannot coexist on a case-insensitive source volume;
these kernel interfaces are outside M2. Internal header aliases are materialized as copies
to make extraction work without filesystem symlink privileges. Runtime source
changes and additional allocator/loader dependencies must be recorded as they
are introduced. A full AOSP platform checkout is not needed for this source audit.

The M2 build compiles the selected/generated libc translation units recorded in
`third_party/bionic/m2-objects.json` into partial relocatable objects. The
upstream profile is unchanged source. The native profile applies the exact,
hash-checked edits in `third_party/bionic/native-boundary.json` to
`libc/platform/bionic/tls.h`, `libc/arch-arm64/bionic/__set_tls.c`,
`libc/bionic/pthread_create.cpp`, `libc/bionic/pthread_exit.cpp` and
`libc/private/bionic_inline_raise.h`, `libc/arch-arm64/bionic/syscall.S` and
`libc/arch-arm64/bionic/vfork.S` in a separate
build overlay. The edits route TLS access through ARTBox endpoints and omit
Android shadow-call-stack setup/cleanup; the signal header uses a fixed-width
raw-syscall endpoint instead of inline kernel entry. These BSD-licensed files retain their
original notices; the patch contexts and derived copies remain subject to those
notices. They are not relicensed as MIT.

Temporary CI object artifacts include `BIONIC-NOTICE.txt`, copied from the
hash-verified aggregate libc notice. They are not linked into the iOS app or
represented as a complete libc. `docs/bionic-build.md` records compiler settings
and remaining native boundaries. Soong's compiler configuration at the same
tag was consulted for build settings, with no Soong source imported into ARTBox.

The Bionic tracing helper uses the unmodified `libcutils/include/cutils/trace.h`
and `compiler.h` headers from [AOSP system/core at android-15.0.0_r1](https://github.com/aosp-mirror/platform_system_core/tree/fc7bc8c4bfb4c6095e34ea784509dae56f25b486),
commit `fc7bc8c4bfb4c6095e34ea784509dae56f25b486`. Both carry Apache-2.0
headers (AOSP copyright 2012 and 2009, respectively). The exact-file pin in
`third_party/sources.json` includes their sizes and SHA-256 hashes and the
complete `libcutils/NOTICE`, including its Apache-2.0 text and AOSP notice.
Only these three files are fetched; the rest of system/core is not imported.
The object artifacts retain that notice as `LIBCUTILS-NOTICE.txt` alongside
the Bionic notice. Linux source-adaptation tests also receive the original and
adapted BSD-licensed signal header with its original notice intact.

The build also runs Bionic's unmodified `libc/tools/gensyscalls.py` against its
original `libc/SYSCALLS.TXT`, then adapts the generated ARM64 kernel entry. The
source hashes are pinned in `third_party/bionic/syscalls.json`. The generic
`syscall.S` carries an AOSP BSD notice. Its included assembler headers retain
their AOSP and Berkeley/OpenBSD/NetBSD notices; the complete libc notice remains
with all object artifacts. Generated/modified Bionic assembly and test symbol
renaming do not relicense that code as MIT. The generator is used as a build
tool from the pinned archive; no rewritten syscall table is substituted.

The native class-library dependency closure selects 57 further unchanged
Bionic units, including its account database, numeric/DNS resolver and syscall
frontends. BSD/ISC/IBM notices remain in the selected files and the complete
Bionic aggregate notice. These are library dependencies, not implementations
of DNS services, network interfaces, epoll or filesystem mutation on Darwin.

The account table is generated by AOSP build's unchanged
`tools/fs_config/fs_config_generator.py` at `android-15.0.0_r1`, commit
`3d9c9aacdb6ca0b745b3048dad88d0db4680d50e`. Its original unit tests and root and
local `Android.bp` files are pinned as exact files. Both package declarations
explicitly select `Android-Apache-2.0`; the Python files do not carry separate
license headers. The input is the previously pinned system/core
`private/android_filesystem_config.h`. One additional header, `cutils/misc.h`,
comes from the same system/core commit `fc7bc8c4bfb4c6095e34ea784509dae56f25b486`
and carries its original Apache-2.0 notice. `FSCONFIG-NOTICE.txt` retains the
package declarations, input/header notices and complete Apache-2.0 terms from
the pinned `libcutils/NOTICE`. It accompanies each signed diagnostic framework.
CI retains the generator, tests, input headers, generated output and their hashes.
These upstream files and their generated data are not relicensed as MIT.

The vfork oracle combines the production adapted assembly with the original
`libc/bionic/__set_errno.cpp`, both carrying AOSP BSD notices. Test-only symbol
prefixing prevents host interposition. Original, adapted and deliberately
mutated assembly copies retain their notices in the artifact's `vfork/`
directory, alongside the complete Bionic notice. The original MIT test callers
inject syscall replies; they do not implement or enable process creation.

The allocator dependencies use the same AOSP `android-15.0.0_r1` tag:

| Component | Immutable AOSP revision | Built source scope |
| --- | --- | --- |
| [Scudo](https://android.googlesource.com/platform/external/scudo/+/67dd481e24c770b9f416f61844c2d015f125c134) | `67dd481e24c770b9f416f61844c2d015f125c134` | 15 standalone units, Android configuration and Bionic C wrappers |
| [GWP-ASan](https://android.googlesource.com/platform/external/gwp_asan/+/df96c8131b0e2cb91e7668a537bda7f18bc25635) | `df96c8131b0e2cb91e7668a537bda7f18bc25635` | Eight allocator, POSIX and crash-handler units; shared units compiled once |

GitHub `aosp-mirror-neo` archives are pinned by size and SHA-256; their commit
IDs were checked against the official AOSP tag. These LLVM-derived sources
carry Apache-2.0 with LLVM exceptions, with legacy permissive license texts in
`LICENSE.TXT`. Scudo's Android configuration also carries an AOSP BSD notice.
`SCUDO-NOTICE.txt` includes the complete license file and unchanged configuration
header; `GWP_ASAN-NOTICE.txt` includes the complete license file. Both are verified
and retained beside the CI objects. The original source notices remain intact.

The project-owned `artbox_gwp_asan_tls.h` uses GWP-ASan's platform-header hook;
it is original MIT integration code. It changes no allocator source file and
does not relicense Scudo or GWP-ASan. The new Linux fixture includes the pinned
GWP-ASan state definition and inline getter in its NDK-built objects, so those
objects retain the corresponding notice. No allocator is shipped in the M1 IPA.

The controlled dynamic-wrapper fixture links the same prefixed Bionic syscall
object into `libartbox_bionic_slice.so`; its probe and wrapper code are original
MIT code. Apple-built `ARTBoxBionicSlice.framework` artifacts include the complete
hash-verified `BIONIC-NOTICE.txt` before signing. This prototype contains Bionic
syscall entries and its errno setter, not a complete libc or the allocators.

The baseline ARM64 memory/string implementations come from
[AOSP Arm optimized routines](https://android.googlesource.com/platform/external/arm-optimized-routines/+/514df029da8aa4146726f6a020ddc69cf34a007d),
commit `514df029da8aa4146726f6a020ddc69cf34a007d` at `android-15.0.0_r1`.
The exact-file pin fetches 14 assembly files, their shared header, Android.bp
and LICENSE (17 files, 66,632 bytes). The implementations carry Arm's
`MIT OR Apache-2.0 WITH LLVM-exception` notice; ARTBox uses the MIT option and
preserves the complete supplied license, including its alternative terms.
The original Bionic dispatcher retains its AOSP BSD notice. No source is
rewritten or relicensed. `ARM-ROUTINES-NOTICE.txt` accompanies the compiled
objects and is included with the Bionic notice before signing the expanded
dynamic fixture. It remains separate from the M1 IPA. The scalar oracle and
host test drivers are original MIT code; symbol prefixing affects test copies.

The property dependency adds the unmodified `libpropertyinfoparser` implementation,
public header and Android.bp, plus `libcutils/include/private/android_filesystem_config.h`
and the complete `libcutils/NOTICE`, from the same pinned system/core commit above.
This is a separate five-file selection named `property-info`; the existing tracing
header selection is unchanged. The parser source/header carry AOSP's 2017
Apache-2.0 notice; the filesystem-ID header carries its 2007 Apache-2.0 notice.
`PROPERTY-INFO-NOTICE.txt` retains the complete license and all three original
source/header notices with object artifacts. Bionic's six system-property units
and public API retain the archive's AOSP BSD notices; FreeBSD wide-string and
OpenBSD duplication helpers retain their original permissive notices in the
Bionic archive and complete libc notice. No property service or proprietary
property data is imported. This source build is not yet property runtime support.

Two target-side compiler-rt members (`comparetf2.c.o` and `multf3.c.o`) come from
NDK r28c's `libclang_rt.builtins-aarch64-android.a`. They supply Android binary128
comparison and multiplication used by Bionic's long-double conversion. Their
exact member hashes and NDK revision are pinned in `third_party/bionic/builtins.json`;
no other archive member is linked. The supplied LLVM toolchain `NOTICE` includes
the Apache-2.0 license with LLVM exceptions and legacy permissive notices. Its
complete, hash-verified text is preserved as `COMPILER-RT-NOTICE.txt` with objects
and the signed arithmetic fixture. The original test caller is MIT. Test symbol
prefixes do not change compiler-rt licensing. The host's floating-point ABI and
arithmetic library are not substituted for these Android runtime functions.

## M3 ART reference review

Reviewed AOSP ART `android-15.0.0_r1`, commit
`bebbc3cc49f2d9d5420197df0a336fbc3fcbea40`, through the
[official source](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40)
and its `aosp-mirror-neo/platform_art` mirror. Its 10,695-byte Apache-2.0
`NOTICE` has SHA-256
`613c3a67424d8f9f32da434d1b98a610f98a7df6c2350c3429d9da975f0405fd`.
Runtime/interpreter build definitions, startup/JIT paths, thread access,
managed-reference storage and low-address mapping code were studied for the
[M3 contract](docs/m3-contract.md). No ART implementation was copied into the
first native address-space probe, which is original MIT-licensed test code.
The `art-references` exact-file pin now selects 12 files (including that notice)
for the `ObjectReference`, `HeapReference` and `CompressedReference` contract.
The original ARTBox fixture is MIT. The AOSP headers retain their Apache-2.0
notices, including in generated source overlays. This selection does not build
the ART runtime, interpreter, collector or class libraries.

Its header dependencies are separate `android-15.0.0_r1` exact-file pins:

- [libbase](https://android.googlesource.com/platform/system/libbase/+/68f963c62f7fddc70c34df141d943ae0f7631020),
  commit `68f963c62f7fddc70c34df141d943ae0f7631020`: five headers and the
  complete Apache-2.0 `NOTICE` (`libbase-references`).
- [fmtlib](https://android.googlesource.com/platform/external/fmtlib/+/360e74bb8ec766ee05e5c4e8956c811391e3e9e5),
  commit `360e74bb8ec766ee05e5c4e8956c811391e3e9e5`: seven headers, MIT
  `LICENSE` and the supplied older BSD-2-Clause `NOTICE` (`fmtlib-references`).
  Both license files are retained; ARTBox does not rely on the optional
  compiled-object exception to remove attribution.

These headers are compiled with NDK r28c's libc++ headers. The complete LLVM
toolchain notice, already hash-pinned in `third_party/bionic/builtins.json`,
accompanies the fixture as `LIBCXX-NOTICE.txt`. The reference objects do not
link additional compiler-rt or libc++ runtime archive members. Reference
frameworks retain `ART-NOTICE.txt`, `LIBBASE-NOTICE.txt`, `FMT-LICENSE.txt`,
`FMT-NOTICE.txt` and `LIBCXX-NOTICE.txt` before signing. Class-library and
broader runtime dependencies still need separate review and selection.

Apple's APSL-2.0 `bsd/kern/mach_loader.c` was consulted to explain the native
ARM64 reduced-pagezero rejection. No kernel code is imported or executed by
ARTBox; the retained negative test and heap-relative codec are original code.

## M3 DEX loader source selection

`art-dex` selects 108 files (987,451 bytes) from the same reviewed ART commit:
16 libdexfile implementation units, 13 libartbase support units, their header
closure, the enum-printer generator, build definitions and complete NOTICE.
The original ARTBox generator emits a fixed DEX data fixture; its caller uses
AOSP's normal loader and verifier API. Neither is a replacement interpreter.
This selection excludes the runtime, compiler and class libraries.

`libbase-dex` extends the same libbase commit above with the selected source
and headers required by the loader. Its complete NOTICE retains both Apache-2.0
and BSD terms. `liblog-dex` selects the host logging implementation and headers
from [system/logging](https://android.googlesource.com/platform/system/logging/+/54d3fa266d2123c0b076c646fae60d049311052a),
commit `54d3fa266d2123c0b076c646fae60d049311052a`, retaining liblog's complete
Apache-2.0 NOTICE. The filesystem-ID header comes from the existing reviewed
`property-info` selection; its libcutils NOTICE is retained as well.

`ziparchive-dex` selects the ZIP reader and support headers from
[system/libziparchive](https://android.googlesource.com/platform/system/libziparchive/+/13b815f485784ed356869746796bb405ec86a7d4),
commit `13b815f485784ed356869746796bb405ec86a7d4`. This tree declares the AOSP
Apache-2.0 license in Android.bp and supplies notices in the source headers
rather than a standalone NOTICE. The pin uses `zip_archive.cc` as its notice
input, retaining its full text alongside the complete Apache-2.0 terms from
ART's NOTICE. ZIP writing and its gtest dependency are not selected.

`jni-dex` contains only `include_jni/jni.h` and NOTICE from
[libnativehelper](https://android.googlesource.com/platform/libnativehelper/+/3ca43dfe2bf4613852df0303531fa8ecf4b7063c),
commit `3ca43dfe2bf4613852df0303531fa8ecf4b7063c`. Both are Apache-2.0; this
provides the types used by libdexfile without importing a Java runtime.
All selections are at the named `android-15.0.0_r1` tag. The existing fmtlib
headers and their two license files are reused. Native host/platform standard
libraries remain platform dependencies; Android object artifacts retain the
pinned NDK toolchain notice. No DEX execution or ART startup is claimed by
compiling these components.

The generated `time_utils.cc` overlay retains its AOSP Apache-2.0 notice and
labels an include-order fix for libstdc++: `<limits>` and `<algorithm>` precede
the header that uses their declarations. The pinned source is unchanged; both
upstream and generated hashes are recorded in the DEX build evidence.

## M3 class-library build inputs

The `*-classlib` selections in `third_party/sources.json` and the build settings
in `third_party/art/classlib.json` use the named AOSP `android-15.0.0_r1` tag.
Each archive and selected file has a recorded hash. Original source headers
remain intact. These inputs build implementation DEX; they do not establish an
ART boot or supply the missing JNI libraries and runtime resources.

| Selection | Upstream commit | License inputs retained |
| --- | --- | --- |
| libcore | `a996d969fdd17c6707b84544e5d1c35e0c25b5cb` | `LICENSE` (GPL-2.0 with the per-file Classpath exception), root `NOTICE`, `ojluni/src/main/NOTICE`, and original Apache/Harmony, MIT/XML and other per-file notices |
| frameworks/libs/modules-utils annotations | `810bd8c57bb39c232cad66e5aa007839dd4e55e3` | Apache-2.0 source/build notices; complete terms accompany the build |
| tools/platform-compat annotations | `df531c7caea306b811fec85a638a371c2a2492d1` | Apache-2.0 source/build notices; complete terms accompany the build |
| ICU Java and libcore bridge | `cf305aeb6df416fa81cfc98bd73d913ee781175d` | Complete root `LICENSE` (Unicode-3.0 and included terms) and original per-file notices |
| Conscrypt Java | `f8e2d81432416b14fbc8e6f62375d9917b044ef1` | Apache-2.0 `LICENSE`, `NOTICE` and source notices |
| Repackaged OkHttp/Okio Java | `fdad2b22b00721086808f155529df11a2e4335f3` | Apache-2.0 `LICENSE.txt`, `okio/LICENSE.txt` and source notices |
| BoringSSL headers for the host constants generator | `23a87e389eb925678c6766f7d0fcd189c2f9303c` | Complete `NOTICE` and `src/LICENSE`, including OpenSSL, SSLeay, ISC and MIT terms |
| R8/D8 host tool | `535c14aebec54e8b67a8ca2889d34cb7c38a29b4` | `LICENSE`, `NOTICE`, `r8.spdx.json`; BSD-3-Clause R8 plus the dependencies listed there |

Libcore's Java sources retain their own licenses; ARTBox's MIT license applies
to original ARTBox code. Class-library binary artifacts must accompany the
selected corresponding source, original notices, build scripts and generated
inputs. The build's source bundle preserves those materials, including the
per-file copyright and permission text. It includes the full Apache-2.0 terms
from Conscrypt's license for selections that supply only source-header notices.

Conscrypt's original C++ generator supplies its 50 native constants using the
pinned BoringSSL headers. ICU's generated `Flags` class contains only the
annotation string keys from `icu.aconfig`; it supplies no runtime flag accessor.
These generated files and their input hashes accompany the artifacts.

D8 and the installed JDK are host build tools. Their binaries are not embedded
in the app or the class-library output. The R8 jar is fetched as an exact Git
blob and checked against SHA-256
`4a84f75723c26d647025204560161bf9e02bdf05700f4014d377393c349f22dc` before execution.
An existing JDK 17 is selected by the builder; its version is recorded. Any
separately installed JDK retains its distribution's own licenses.

The optional portable JDK download selects Eclipse Temurin `17.0.20.1+1` from
`adoptium/temurin17-binaries`, with platform archive sizes and SHA-256 values in
`third_party/jdk.json`, cross-checked against the release's checksum files.
The complete distribution is retained under the configured tool cache,
including GPL-2.0, the Classpath and assembly exceptions, and bundled dependency
notices under `legal/`. It is a host compiler and is not redistributed in the
class-library or app artifacts.

## M3 runtime policy test inputs

`art-runtime-policy` selects the JIT factory declaration, its macro header and
NOTICE from the same Apache-2.0 ART commit
`bebbc3cc49f2d9d5420197df0a336fbc3fcbea40`. The factory implementation is original
MIT ARTBox code. It does not include any ART compiler implementation.

`unwindstack-demangle` selects `Demangle.cpp`, its public header, Android.bp and
`LICENSE_BSD` from AOSP system/unwinding commit
`63e40770259ea336f17be2ba6af791e89419169c`, tag `android-15.0.0_r1`. The selected
source/header carry Apache-2.0 notices; the module's two-clause BSD notice is
also retained, alongside the full Apache text from ART. The generated overlay
keeps its original notice and labels the optional Rust-formatting change.

`rust-demangle-test-header` selects only the C declaration and both MIT/Apache
license texts from AOSP rustc-demangle-capi commit
`4037ffd297333c120ea13e9c8809f5a24adc4317`, at the same named tag. The original
control build uses that declaration with a test-only callback that records
invocations. No Rust implementation, compiler or runtime is built or shipped.
The adapted binary contains no such callback. Existing libbase and fmt headers
retain their previously recorded notices. Every selected file is pinned in
`third_party/sources.json` and the binary evidence preserves the license texts.

## M3 interpreter runtime source selection

`third_party/art/runtime-sources.json` supplements the existing DEX, logging,
fmt and JNI selections with the original runtime sources and required support
headers/libraries. `scripts/art_runtime_sources.py` fetches and verifies these
inputs in the configured cache. Source preparation does not build or execute ART.
The AOSP selections below use `android-15.0.0_r1`; every selected file has an
individual SHA-256 in addition to any archive hash.

| Component | Commit | Retained terms |
| --- | --- | --- |
| ART runtime, support libraries and ARM64 entrypoints | `bebbc3cc49f2d9d5420197df0a336fbc3fcbea40` | Apache-2.0 NOTICE and original per-file notices |
| CPU features | `eca53ba6d2e951e174b64682eaf56a36b8204c89` | Complete LICENSE and source notices |
| dlmalloc | `f18b9ade29f2be5126c85918c2354f1e09735220` | Public-domain NOTICE and original source headers |
| Additional liblog event functions | `54d3fa266d2123c0b076c646fae60d049311052a` | Apache-2.0 `liblog/NOTICE` |
| LZ4 library | `1d69e78024385c335dc5b42752b96623c31d8e5d` | BSD-2-Clause `lib/LICENSE` and root license description; CLI excluded |
| LZMA SDK C library | `b744809d5506de926da56108065051d970692ec7` | Public-domain NOTICE and marker; 7-Zip C++ and unRAR excluded |
| libnativehelper headers | `3ca43dfe2bf4613852df0303531fa8ecf4b7063c` | Apache-2.0 NOTICE and source headers |
| procinfo map parser header | `e2d8c69e0454afe6defccb62152738888a1cbd22` | Apache-2.0 header notice, with complete terms from ART's NOTICE |
| tinyxml2 | `d3f5ba7b82e0f97c06dc8eca7048cd5098323c2f` | Zlib-style `LICENSE.txt` |
| libunwindstack | `63e40770259ea336f17be2ba6af791e89419169c` | Original Apache-2.0 source notices, module `LICENSE_BSD` and complete Apache terms from ART |
| zlib | `86056a43326ceb4d9f5c3ca2410e51dcdcb4517d` | Zlib LICENSE and Chromium BSD-3-Clause terms for selected SIMD code |
| zstd library | `34edb25604da376a8a951c34801734ffb6ce662d` | BSD-3-Clause LICENSE selected; alternative GPL COPYING retained; CLI excluded |

The complete Chromium license text is separately pinned at commit
`249765936393e6222039dfbfc05f8ace9e561f32`; this selection contains only LICENSE.
Any runtime binary distribution must retain these complete terms and original
per-file notices. The source catalog imports no ART compiler implementation or
Rust demangler implementation. Existing NDK static libraries remain toolchain
inputs subject to the NDK notices; source selection does not establish their
suitability for Apple's register, TLS or unwinding conventions.

## Native Java library dependencies

`third_party/art/native-library-sources.json` extends the same reviewed AOSP
`android-15.0.0_r1` pins for the native Java dependency build:

| Component | Commit | Retained terms |
| --- | --- | --- |
| Seven libnativehelper implementations and their headers | `3ca43dfe2bf4613852df0303531fa8ecf4b7063c` | Complete Apache-2.0 NOTICE and per-file notices |
| ICU common/i18n, native JNI bridge, registration, C API shim and ICU 75 data | `cf305aeb6df416fa81cfc98bd73d913ee781175d` | Root LICENSE, icu4c/LICENSE, icu4c/license.html and original per-file notices |

The ICU implementation retains its Unicode/ICU and included permissive terms;
the AOSP bridge and registration sources retain their Apache-2.0 notices. The
native build preserves all selected corresponding source, including the data
file, without modifying these upstream sources. Existing libbase, liblog, fmt
and NDK dependencies retain their previously listed terms. The Linux output
also includes the verified ART dependency's complete corresponding-source
archive. No Google services or vendor implementation is selected.

`third_party/art/libcore-native-sources.json` adds the remaining native class
library implementations at the same `android-15.0.0_r1` tag:

| Component | Commit | Retained terms |
| --- | --- | --- |
| Libcore JNI and OpenJDK native implementations | `a996d969fdd17c6707b84544e5d1c35e0c25b5cb` | Complete LICENSE and NOTICE, GPL-2.0 with Classpath exception for designated OpenJDK sources, and original Apache/per-file notices |
| ART OpenjdkJvm implementation | `bebbc3cc49f2d9d5420197df0a336fbc3fcbea40` | Original GPL-2.0 with Classpath exception source notice and openjdkjvm/LICENSE; full GPL terms accompany libcore |
| fdlibm | `1e651e1ef2b613db2c4b29ae59c1de74cf0222ae` | Sun's permission notice in NOTICE and each selected source |
| BoringSSL libcrypto_for_art subset | `23a87e389eb925678c6766f7d0fcd189c2f9303c` | Complete NOTICE, src/LICENSE and original per-file notices, including OpenSSL, SSLeay, ISC and MIT terms |
| Expat library | `8ae3fff00472acf17b96871c7cdeaccfa7430c73` | MIT root COPYING, expat/COPYING and original source notices |

The signed Android libcore link additionally selects `udivti3.c.o`,
`umodti3.c.o` and `udivmodti4.c.o` from the already pinned NDK r28c compiler-rt
archive. `third_party/art/libcore-builtins.json` pins each member and the
complete NDK notice (Apache-2.0 WITH LLVM-exception for these helpers). A narrow
archive prevents selection of unreviewed members; the helpers remain local to
their consuming library. Original ARTBox integer test vectors and callers are
MIT. This does not change any upstream component's license.

The native libcore artifacts retain all selected corresponding source and build
inputs, including the original relative-header layout recipe. No license header
is removed; ARTBox's MIT license applies to original ARTBox code. The Expat
selection excludes its documentation and tools. Its canonical `expat/lib` paths
avoid depending on the upstream directory symlink.

The native Linux build also selects the original Bionic `sys/capability.h` and
complete `libc/NOTICE` at the already reviewed commit
`361ba86734fb2821a6adcfdf775db8abd04e0de0`. The header retains its BSD-2-Clause
notice. Its Linux `syscall` forwarding definitions are original MIT ARTBox code;
the host's own Linux UAPI headers supply the kernel structures.

The optional managed-window ART profile applies hash-checked edits from
`third_party/art/managed-window-boundary.json` to already selected Apache-2.0
MemMap, heap and DiscontinuousSpace sources. It retains their original notices and archives the
adapted files with the original source. Its heap bridge and test fixture are
original MIT ARTBox code. This profile adds no upstream component or new license.

`third_party/art/class-table-boundary.json` also adapts the already selected
Apache-2.0 class-table inline header at the same ART pin. Its pointer conversion
uses the checked heap codec while retaining hash tags and atomic updates. The
original header, notice and adapted bytes accompany the runtime artifact.

## LLVM register context assembly

The `unwind-context` selection in `third_party/sources.json` pins four files from
LLVM commit `3b5e7c83a6e226d5bd7ed2e9b67449b64812074c`, the base revision recorded
by Android NDK r28c's `clang_source_info.md`. The register save/restore assembly
and its macro header use **Apache-2.0 WITH LLVM-exception**. The complete
`libunwind/LICENSE.TXT` is retained; original source headers must remain intact.
The ARTBox context test and native runners are original MIT code.

Reassembling the original ARM64 routines with the pinned NDK reproduces the
144-byte restore and 156-byte save text sections from its shipped archive.
This establishes a source basis for adapting reserved-register restoration;
it does not establish Apple unwinder execution or general exception support.

## AOSP libdl frontend

`third_party/bionic/libdl.json` selects the unchanged `libdl/libdl.cpp` at the
existing Bionic commit `361ba86734fb2821a6adcfdf775db8abd04e0de0`.
Its seven selected APIs use **Apache-2.0**; the complete `libdl/NOTICE`, original
source header and `Android.bp` license declaration are retained and hash-checked.
The corresponding-source archive contains those files and the ARTBox bridge and
build inputs. Every signed framework includes the libdl notice, the pinned NDK
notice covering its compile-time headers, and ARTBox's MIT license. No upstream
code is copied into original ARTBox source files.

The full Android ART guest link also uses the pinned NDK r28c's
`libc++_static.a`, `libc++abi.a`, `libunwind.a`, compiler-rt builtins and Android
CRT objects. Their hashes and extracted archive members are recorded, and the
complete hash-verified NDK toolchain NOTICE accompanies the runtime frameworks.
The selected LLVM libraries retain their Apache-2.0 WITH LLVM-exception and
other per-component terms as listed there. The already reviewed source-built
context restore replaces the archive's platform-register restore; its original
and adapted source and license accompany the link artifact. Original ARTBox
glue remains MIT, and Android/host C++ runtime objects are not shared.

`third_party/art/native-probe-boundary.json` adapts the already selected
Apache-2.0 `runtime/gc/collector/mark_compact.cc` at the same ART pin. Original
and adapted source, notices and the extracted feature-probe test input accompany
native guest build artifacts. The injected-syscall fixture is original MIT code;
no additional upstream dependency is introduced.

The ICU dependency expansion selects unchanged `strcat`, `strncat` and `div`
sources from Bionic's OpenBSD subtree (BSD-3-Clause), plus FreeBSD-subtree
`expf`, `tanhf`, `expm1f` and `modf` sources carrying Sun's permissive
notice-preservation terms. All remain at the existing Bionic pin. Their source
headers and the complete Bionic libc/libm notices accompany the artifacts.

The native libcore dependency expansion adds unchanged gethostname and
__cmsg_nxthdr (AOSP BSD terms), bsearch (NetBSD BSD-3-Clause), strtok and setenv
(OpenBSD BSD-3-Clause), and inet_pton (Internet Software Consortium permissive
terms). Bionic's FreeBSD log1p and remainder retain Sun's notice-preservation
terms. All use the existing Bionic pin. Original per-file notices and the full
libc/libm notices remain in corresponding-source and binary artifacts.

The standalone signal-chain reference uses unchanged `sigchainlib/sigchain.cc`,
`sigchain.h` and `log.h` from the existing ART Android 15 pin (Apache-2.0).
Their original headers and hash-checked ART NOTICE accompany its source archive.
The signed ART guest already includes this implementation; its additional caller
is original MIT code. The Linux reference links the runner's system C++ runtime
and libc and does not represent an Android binary.

The signed JavaVM startup selection additionally pins unchanged
`runtime/runtime_android.cc` from the same ART revision (Apache-2.0). Its
original header and the complete ART NOTICE remain in corresponding-source
archives and signed frameworks. This selects upstream Android platform
initialization; no third-party implementation is copied into the original
MIT JNI invocation fixture.
