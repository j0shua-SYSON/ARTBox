# Native libcore build

ART's boot class path requires real `libjavacore`, `libopenjdk`, and their native
dependencies. This build adds those original AOSP implementations to the
[ART runtime](m3-runtime-build.md) and [ICU build](m3-native-libraries.md).
Compilation and native dependency tests do not establish Java execution.

```console
python -B scripts/build_art_runtime.py --profile android
python -B scripts/build_art_libcore.py --profile android
python -B scripts/build_art_libcore.py --profile android --all
python -B scripts/build_art_runtime.py --profile linux --all --link
python -B scripts/build_art_native_libraries.py --profile linux --all --link
python -B scripts/build_art_libcore.py --profile linux --all --link
```

The default preflight compiles 12 upstream units; `--all` compiles 208: 21 javacore,
one androidio, 61 OpenJDK, 80 fdlibm, 41 BoringSSL, three Expat, and one
OpenjdkJvm unit. The source catalog selects 458 original file entries at the reviewed
`android-15.0.0_r1` pins. ICU, nativehelper and support headers reuse the existing
selections. Compiler, archiver, toolchain, build, runtime and native dependency
paths are configurable. The Linux reference requires native ARM64 Linux.
It also compiles one small host bridge for `capget`/`capset`, bringing its
preflight/full compilation totals to 13/209 units. Linux uses the native `ar`
command by default, with an `AR`/`--ar` override; Android uses the NDK archiver.
The link build checks and records the selected archiver before compilation.

OpenjdkJvm uses ART's recorded compiler/header configuration, including its
checked host source adaptations. Build ART in the same workspace first; moving
its recorded include directories requires rebuilding it. The builder stages
unchanged ART and libcore sources with their original sibling layout, and copies
the original fdlibm header to libcore's expected relative include path. It does
not modify the source cache. ICU's generated NDK headers precede its internal
headers; the original `LIBICU_U_SHOW_CPLUSPLUS_API` switch exposes the C++ helper
types used by libcore.
The AOSP `ANDROID` build marker activates the NDK header's original local
configuration, which disables symbol renaming for the `libicu` C API shim.
It does not change the separate `__ANDROID__` target-OS macro. The Linux
implementation libraries retain their versioned `_75` symbols behind the shim.

Strict C11 needs `_GNU_SOURCE` for BoringSSL's Linux pthread declarations.
OpenjdkJvm explicitly includes the standard math header for its `isnan` call.
The host uses Bionic's original capability declarations with the system's Linux
UAPI types; the bridge forwards both calls to the real kernel through `syscall`.
This supplies a missing glibc interface without installing libcap or changing
capability behavior. It is not an implementation of iOS capabilities.

The Linux build links `libandroidio.so`, `libopenjdkjvm.so`, `libjavacore.so`,
`libopenjdk.so` and `libexpat.so`, with undefined-symbol checks enabled. fdlibm
and BoringSSL's upstream `libcrypto_for_art` selection remain static archives;
only required members enter the JNI libraries. BoringSSL uses its portable C
path with assembly disabled. The source set does not provide a general TLS
stack or imply any crypto certification. The Linux ART reference already
exports the selected base/log, ZIP and zlib support used by these libraries.
The original javacore export map keeps its JNI class cache local: OpenJDK
defines identically named cache functions for a different class set. Only the
upstream JNI load/unload entrypoints are public from javacore.

The native fixture checks SHA-1's `abc` vector, modular exponentiation, exact
math results, signed zero, NaN and infinity, incremental XML parsing and malformed
XML rejection. It exercises original JVM file operations and raw monitors across
two pthread-backed C++ threads, then loads both JNI libraries and checks their
`JNI_OnLoad` exports and javacore's hidden class cache. It checks JVM NaN
classification and compares capability reads and invalid requests with raw
Linux syscalls. Invalid `capset` requests cannot change process privileges.
It also calls the unversioned ICU shim to check its version and malformed UTF-8
substitution through the same headers used by libcore. These are 21 native
cases. Calling JNI registration and executing the boot
classes still require a real ART startup test.

Artifacts preserve source/object hashes, compiler and link commands, native test
output, original notices, all selected corresponding source, and the verified
runtime/ICU source bundles. This is a Linux reference dependency build. Apple
managed references, TLS, signals, native ABI and signed code integration remain
part of M3 acceptance.
