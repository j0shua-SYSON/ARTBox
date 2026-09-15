# ART runtime build

The runtime builder compiles the selected AOSP interpreter, support libraries
and ARM64 entrypoints. It has two configurations: Android ELF objects for the
Bionic integration, and a native Linux host reference. It does not run ART.
M3 remains incomplete until the execution checks in [the contract](m3-contract.md)
pass and the runtime is integrated into the signed iOS app.

```console
python -B scripts/build_art_runtime.py --profile android
python -B scripts/build_art_runtime.py --profile android --all
python -B scripts/build_art_runtime.py --profile linux --all --link
```

The default is a four-unit compilation preflight. `--all` selects 458 units;
the Linux `--link` option additionally builds `libart.so` and a JNI invocation
harness. The Linux configuration requires native ARM64 Linux with Clang C/C++.
The Android configuration uses the pinned NDK on Windows, macOS or Linux.
Compiler, NDK, build directory and job count are configurable. Environment and
cache settings follow `scripts/environment.py`.

The source selection is pinned at AOSP `android-15.0.0_r1`. The builder regenerates
the assembly offsets, three enum printers and nterp assembly from original AOSP
inputs. C++ switch interpretation is selected by the runtime's `-Xint` option;
compiling nterp satisfies original entrypoint references without selecting it
for execution. The JIT compiler implementation is excluded. Its factory fails
if called, and optional Rust trace formatting remains disabled under ADR 0036.
The previously tested `time_utils` include-order fix is reused.

The Linux build applies six hash-checked source edits in a separate copy of the
selected ART tree. They add missing standard declarations, qualify `nullptr_t`,
probe the host's `strlcpy` declaration and library, and accept runtime-valued
thread/signal stack minima. Nine portable stack-size cases reject invalid minima
and preserve the requested size; five native copy/truncation cases check the
selected `strlcpy`. The original cache and notices remain intact, and changed
sources are included in the corresponding-source archive. See ADR 0038.

The Android build still uses original Bionic TLS and absolute ART references.
It is an ABI build input, and cannot yet run through the Apple boundary. The
Linux configuration uses its native host ABI and original ART reference
representation. These configurations must keep their C++ dependencies separate.
See ADR 0037 for the static Bionic and public NDK link limitations.

Build outputs preserve source and object hashes, complete compiler commands,
generated inputs, notices and `corresponding-source.zip`. A successful link
does not prove startup, JNI registration, DEX execution, signal handling,
collection or absence of runtime executable-memory requests. The harness is
prepared to call the original `artbox.Hello.message()` method; it is not an
acceptance result until executed and checked under the M3 contract.
