# Third-party provenance

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
servicemanager `main.cpp` at that tag use Apache-2.0. Neither has been imported.
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
`libc/private/bionic_inline_raise.h` and `libc/arch-arm64/bionic/syscall.S` in a separate
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
ART and each class-library/build dependency still require explicit source pins
and retained notices before compilation or redistribution.

Apple's APSL-2.0 `bsd/kern/mach_loader.c` was consulted to explain the native
ARM64 reduced-pagezero rejection. No kernel code is imported or executed by
ARTBox; the retained negative test and heap-relative codec are original code.
