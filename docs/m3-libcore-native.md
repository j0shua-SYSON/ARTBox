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
substitution through the same headers used by libcore. The expanded fixture adds
a 22nd group for OpenJDK's POSIX error-string helpers: successful and truncated
buffers, zero-error/zero-length no-ops, terminators, guard bytes and errno
preservation. All 22 groups pass in both native Linux runtime profiles at
`030b310`. Calling JNI registration and executing the boot
classes still require a real ART startup test.

The Android compilation of the unchanged `jni_util_md.c` undefines
`_GNU_SOURCE` for that unit. Its Linux branch otherwise selects glibc's
`__xpg_strerror_r`, which Bionic does not export. The source requires an integer
return value, so use Bionic's POSIX declaration and existing `strerror_r` symbol;
do not bind it to the pointer-returning GNU variant. A strict local compile/link
probe reproduces the missing glibc alias before this flag and resolves against
the real guest libc afterward. All 208 Android units compile with this selection.
Linux retains its original compiler flags. The Android-built helper passes
through the signed Apple runtime at `c3fd5aa`.

OpenJDK also imports vfork. The selected AOSP frontend now resolves from the
281-unit Bionic build, with its TLS read and kernel entry adapted to the existing
bridges. Preserve the original cached PID/vfork bits and errno behavior while
returning ENOSYS for raw process clone. At `3826391`, both signed Mac modes pass
28 injected-state cases and two real guest rejection checks; native Linux
confirms the captured cases and detects a missing-register-save mutation.
This satisfies the frontend contract, not subprocess support. Additional
Bionic dependencies are now selected: at `1647514`, the 338-unit build passes
75 new frontend/account checks in both signed Mac modes. Additional syscall
services remain open even when their Bionic frontend links.

The Android guest job now also builds all 208 units and a guest variant of the
native fixture. `scripts/link_libcore_guest.py` verifies producer revisions,
object/source hashes, every library's reachable imports and the instruction
boundary before creating signed Mac and iOS 15 frameworks. It preserves the
original javacore export map. OpenjdkJvm retains exactly the existing explicit
`artbox_bionic_get_tls` boundary; no new host implementation is supplied.

Three pinned NDK compiler-rt unsigned-128 division members form a narrow local
archive. Neither libc nor the class libraries export these helpers. The same
bytes are tested against 228 Python-generated quotient/remainder vectors in
the signed caller; the 128-bit calling convention never crosses into Apple C.
The generated vectors, original test source, member hashes and complete NDK
notice accompany the producer artifact.

The shared runner loads 15 images: nine ART/ICU dependencies, five class
libraries and the test caller. The Apple fixture requires 17 applicable native
groups; the five Linux capability groups remain mandatory in the 22-group
Linux reference. It calls real JVM file and monitor routines, joins and reaps
the guest worker, checks both JNI library exports and the POSIX error-string
ABI, and reports separate native/integer test times. JNI_OnLoad and JavaVM are
not invoked. At `3505832`, the six libraries link and sign but the actual JVM
temporary-file cleanup fails on unimplemented unlinkat. The rooted unlink
extension retains this test and adds a 29-case native comparison for pathname
removal and open-inode lifetime. All 29 cases pass in both signed Mac modes
and both native Linux profiles at `c211a46`. That rerun reaches the hidden-cache
assertion after the file and monitor groups: javacore's own export map is correct,
but an unnecessary dependency on libicu_jni exposes ICU's public cache copy.
Use `--as-needed` for the five production libraries, retaining the caller's
explicit full manifest. A dependency-scope check rejects both direct and transitive
JniConstants exposure from javacore. Keep the existing runtime assertion; neither
the class cache nor dlsym behavior is special-cased.

At `c3fd5aa`, the signed Mac runner passes all 17 native groups and 228 integer
vectors, returns from 38 constructors, joins/reaps one worker and releases its
resources. Independent downloaded-artifact checks verify all 208 objects plus
the caller, 1,666 upstream inputs, 154 canonical project files, 131,813 native
instructions, twelve Mac/iOS framework layouts and 204 notice hashes. All nine
base-library ELF hashes and dynamic metadata match the execution inputs.
Signature verification runs in Apple CI; local verification checks bytes,
layouts and signature presence. Physical device execution remains unverified.

One traced run records 48.201 ms for load/relocation, 1.560 ms for bootstrap,
0.577 ms for the native groups and 0.039 ms for integer vectors. These are
correctness observations, not application throughput or iPhone measurements.
Neither JNI_OnLoad nor JavaVM startup is exercised by this result.
The complete [host workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36308398246)
and [iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36308398345)
pass. The integrated IPA still contains the M1/M2 diagnostics; the new class
libraries are separate signed framework artifacts until Apple JavaVM acceptance.

Artifacts preserve source/object hashes, compiler and link commands, native test
output, original notices, all selected corresponding source, and the verified
runtime/ICU source bundles. This is a Linux reference dependency build. Apple
managed references, TLS, signals, native ABI and signed code integration remain
part of M3 acceptance.
