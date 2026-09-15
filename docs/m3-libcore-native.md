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

The default preflight compiles 12 units; `--all` compiles 208: 21 javacore,
one androidio, 61 OpenJDK, 80 fdlibm, 41 BoringSSL, three Expat, and one
OpenjdkJvm unit. The source catalog adds 452 original files at the reviewed
`android-15.0.0_r1` pins. ICU, nativehelper and support headers reuse the existing
selections. Compiler, archiver, toolchain, build, runtime and native dependency
paths are configurable. The Linux reference requires native ARM64 Linux.

OpenjdkJvm uses ART's recorded compiler/header configuration, including its
checked host source adaptations. Build ART in the same workspace first; moving
its recorded include directories requires rebuilding it. The builder stages
unchanged ART and libcore sources with their original sibling layout, and copies
the original fdlibm header to libcore's expected relative include path. It does
not modify the source cache. ICU's generated NDK headers precede its internal
headers; the original `LIBICU_U_SHOW_CPLUSPLUS_API` switch exposes the C++ helper
types used by libcore.

The Linux build links `libandroidio.so`, `libopenjdkjvm.so`, `libjavacore.so`,
`libopenjdk.so` and `libexpat.so`, with undefined-symbol checks enabled. fdlibm
and BoringSSL's upstream `libcrypto_for_art` selection remain static archives;
only required members enter the JNI libraries. BoringSSL uses its portable C
path with assembly disabled. The source set does not provide a general TLS
stack or imply any crypto certification. The Linux ART reference already
exports the selected base/log, ZIP and zlib support used by these libraries.

The native fixture checks SHA-1's `abc` vector, modular exponentiation, exact
math results, signed zero, NaN and infinity, incremental XML parsing and malformed
XML rejection. It exercises original JVM file operations and raw monitors across
two pthread-backed C++ threads, then loads both JNI libraries and checks their
`JNI_OnLoad` exports. These are 13 native cases. Calling JNI registration and
executing the boot classes still require a real ART startup test.

Artifacts preserve source/object hashes, compiler and link commands, native test
output, original notices, all selected corresponding source, and the verified
runtime/ICU source bundles. This is a Linux reference dependency build. Apple
managed references, TLS, signals, native ABI and signed code integration remain
part of M3 acceptance.
