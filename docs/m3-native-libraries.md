# Native Java library dependencies

ART startup loads `libicu_jni`, `libjavacore` and `libopenjdk`. Building the boot
class path does not provide these native implementations. This first dependency
build selects real AOSP nativehelper and ICU sources at `android-15.0.0_r1`.
Native libcore, its math/crypto/XML dependencies, and ART startup follow.

```console
python -B scripts/build_art_native_libraries.py --profile android
python -B scripts/build_art_native_libraries.py --profile android --all
python -B scripts/build_art_runtime.py --profile linux --all --link
python -B scripts/build_art_native_libraries.py --profile linux --all --link
```

The default compilation preflight selects 29 units, including every nativehelper
and ICU JNI unit. `--all` selects 480 units: seven nativehelper sources and 473
ICU implementation/registration sources. The original source selection contains
1,066 files, including headers, notices and the pinned ICU 75 data file. Compiler,
toolchain, build and source-cache paths are configurable through command-line
options and `scripts/environment.py`.

The Linux build requires native ARM64 Linux. It produces `libnativehelper.so`,
`libicuuc.so`, `libicui18n.so`, `libicu.so` and `libicu_jni.so`. The monolithic
Linux ART reference already exports the selected base/log support functions;
the JNI dependencies link against that verified `libart.so`. Its hash-checked
binary and complete corresponding-source bundle accompany this build. This is
a host dependency arrangement, not an Apple packaging result.

ICU retains RTTI as specified by its upstream build; C++ exceptions remain
disabled. Nativehelper's Linux C11 build requests POSIX.1-2008 declarations,
including the XSI `int`-returning `strerror_r` used by its unchanged source.
The `ANDROID` build marker enables ICU's original AOSP host registration path;
the separate `__ANDROID__` OS-ABI macro remains unchanged for Linux. Registration
requires `ANDROID_DATA`, `ANDROID_TZDATA_ROOT` and `ANDROID_I18N_ROOT`, all rooted
in the build directory. It maps the original `icudt75l.dat` from the I18N root;
no separate timezone update is staged. The native check exercises version and
data initialization, Unicode conversion, malformed UTF-8 rejection, Turkish
case mapping, collation, regular expressions, and loading the JNI library with
an exported `JNI_OnLoad`. It does not call that entrypoint without a Java VM.

Source/object hashes, compiler commands, link diagnostics, test output, notices
and complete selected corresponding source are retained. ART has not booted or
executed DEX as part of this check. The Android object profile still requires
Apple TLS, managed-reference, signal and signed-library integration.

The Android build now feeds `scripts/link_icu_guest.py`. Its inputs are the
480-object native dependency build, full ART link and Bionic/math/libdl evidence
from one Git revision. The script verifies producer source and object hashes,
links the five libraries with strong imports required, and checks each library's
actual `DT_NEEDED` closure. A symbol exported by an unrelated image cannot
satisfy an import. Base/log/C++ symbols come from the same guest ART library;
no Apple C++ runtime objects enter the Android libraries.

These ICU objects have no compiler TLS. Their linker script asserts that TLS
sections stay empty instead of emitting an invalid empty `PT_TLS`. Instruction
checks reject SVC, thread-pointer accesses, x18/x27/x28 use and undecoded
instructions. The existing wrapper checks the RX/RW layout before signing
separate Mac ARM64 and iOS 15 frameworks with no entitlements. Original ICU
data, dependency source archives and notices accompany the result.

```console
python -B scripts/link_icu_guest.py --native-dir build/m3/guest-icu-inputs --guest-dir build/m3/art-guest-link --dependency-dir build/m3/guest-dependency-inputs
```

At `d68dd3e`, all five libraries link and sign as ten Mac/iOS frameworks in
[host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36297239640), with
the complete workflow and both Linux ART execution profiles passing. This
packaging stage does not execute ICU.

The next execution test adds a separate original guest caller to the 480 upstream
objects and packages it as `ARTBoxICUCheck`. It reuses the eight Linux ICU test
groups through a fixed-word C entry, keeping C++ and variadic calls in the Android
ABI. The shared runner loads ten images rooted at that caller, initializes
Bionic and all constructors, then checks ART's pre-start boundary and ICU.
The rooted filesystem supplies `/system/i18n/etc/icu/icudt75l.dat`; Android data,
I18N and timezone environment variables are set before constructors run.
`u_cleanup` releases caches and the original AOSP data mapping before VM teardown.

```console
python -B scripts/test_icu_guest.py --icu-dir build/m3/icu-guest-link --guest-dir build/m3/art-guest-link --dependency-dir build/m3/guest-dependency-inputs
```

At `ecf9000`, the signed Mac ARM64 execution passes all eight groups, with 33
constructors, ten loaded images, one TLS module and successful cleanup. Independent
downloaded checks verify 141 canonical project inputs, 480 upstream objects plus
the caller, 1,149 upstream source files, 639,501 decoded instructions, twelve
framework layouts and 84 notice hashes. The 27,956,384-byte ICU data matches its
pinned hash. The execution report additionally verifies the runner and its 64
source inputs against the producer revision.

| Mac diagnostic phase | One observed run |
| --- | ---: |
| Load, relocate and establish loader services | 41.313 ms |
| Stack/thread setup, constructors, pre-start checks, ICU and join | 2.173 ms |
| ICU test entry, including data initialization and cleanup, within the preceding phase | 1.381 ms |

These are single-run diagnostics, not cold-start benchmarks or iPhone results.
The [signed execution job](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36298158383/job/108561531133)
also retains the original four-image ART bootstrap and both M2 modes. The test
looks up `JNI_OnLoad` without invoking it. No JavaVM starts and no DEX runs on
Apple as part of this check; those remain M3 requirements. The integrated IPA
still contains M1/M2 diagnostics without ART or ICU.
