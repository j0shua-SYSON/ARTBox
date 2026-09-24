# ART runtime build

The runtime builder compiles the selected AOSP interpreter, support libraries
and ARM64 entrypoints. It has two configurations: Android ELF objects for the
Bionic integration, and a native Linux host reference. It does not run ART.
M3 remains incomplete until the execution checks in [the contract](m3-contract.md)
pass and the runtime is integrated into the signed iOS app.

The separate [startup probe](m3-runtime-startup.md) executes the original hello
DEX on native Linux ARM64 at `7d0ed24`. Apple ART execution remains incomplete.

```console
python -B scripts/build_art_runtime.py --profile android
python -B scripts/build_art_runtime.py --profile android --all
python -B scripts/build_art_runtime.py --profile linux --all --link
```

The default is a four-unit compilation preflight. `--all` selects 459 units,
including the original ARTBox thread-state acceptance helper inside libart;
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

The runtime uses D8 desugaring and AOSP's default zero initialization of automatic
storage (`-ftrivial-auto-var-init=zero`). The original class linker supplies stack
memory to a bitmap without clearing it explicitly. Omitting this compiler policy
can make unrelated stack contents appear to be already assigned virtual methods.
The Linux builder checks an automatic array and four dynamic allocation sizes
with its actual runtime flags before compiling ART. A pattern-initialized
negative control must reject all five cases. Commands, binaries, logs and source
hashes are retained. See ADRs 0044 and 0045.

The Linux build adapts six hash-checked source files in a separate copy of the
selected ART tree. They add missing standard declarations, qualify `nullptr_t`,
probe the host's `strlcpy` declaration and library, and accept runtime-valued
thread/signal stack minima. Bootstrap failures also report the linked class layout.
Nine portable stack-size cases reject invalid minima
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

## Verified build at e6e69d5

[Host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34950970550) and
[iOS CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34950970484) pass.
Native Linux ARM64 compiles all 458 units, passes the five host `strlcpy` cases,
and links both the runtime and invocation harness. The portable checks include
nine stack-minimum cases and source-drift rejection. All 458 original Android
objects also compile in the local preflight builder.

The downloaded runtime artifact has SHA-256
`25c9bf762dcaee6a70313ea11c262c52fe07375f50959977284a0aad059e6de5`.
All object hashes, source pins, generated inputs, notices and project source
bytes match the recorded build. The source archive has SHA-256
`1894dc07847afa3bf0f1401417baa3a77fc24d810d34f6eced5057dfc718e329`.

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `libart.so` | 14,195,504 | `ee9af99b817fe65050a61ee0b8ca8232c9694d62564152dff671619f2602f142` |
| `art-linux-reference` | 71,464 | `0988de7beee4cb4cd160ef5dd951cd0ca0a992d933a65f8019549c1dded0c8a2` |

Both outputs are ARM64 ELF files with no writable executable load segment or
executable GNU stack. Linking the runtime and compiling/linking the harness
together took 0.753 seconds on this runner. This is a build observation, not
ART startup or interpreter performance. ART execution and runtime mapping
behavior remain unverified.
