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
