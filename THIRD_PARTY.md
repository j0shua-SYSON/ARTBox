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

## M2 source pin (runtime integration in progress)

Bionic is pinned to [android-15.0.0_r1](https://github.com/aosp-mirror/platform_bionic/tree/361ba86734fb2821a6adcfdf775db8abd04e0de0)
at commit `361ba86734fb2821a6adcfdf775db8abd04e0de0`, from AOSP's GitHub mirror.
`third_party/sources.json` records the retrieved archive and libc notice hashes.
The source includes BSD and other permissive notices; libdl has an Apache-2.0
notice. Preserve component notices and individual source headers. Bionic is not
relicensed as MIT. No Bionic runtime is shipped by the current M1 app.

The source fetch step omits editor configuration, header-versioner tooling and
unused netfilter kernel headers listed in the lock file. Netfilter's
case-colliding filenames cannot coexist on a case-insensitive source volume;
these kernel interfaces are outside M2. Internal header aliases are materialized as copies
to make extraction work without filesystem symlink privileges. Runtime source
changes and additional allocator/loader dependencies must be recorded as they
are introduced. A full AOSP platform checkout is not needed for this source audit.

The M2 build now compiles the 33 unchanged libc translation units listed in
`third_party/bionic/m2-objects.json` into a partial relocatable object. The
temporary CI object artifact includes `BIONIC-NOTICE.txt`, copied from the
hash-verified aggregate libc notice. It is not linked into the iOS app or
represented as a complete libc. `docs/bionic-build.md` records compiler settings
and the native boundaries still requiring adaptation; no source license has
changed. Soong's compiler configuration at the same tag was consulted for
build settings, with no Soong source imported into ARTBox.
