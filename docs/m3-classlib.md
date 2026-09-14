# AOSP implementation class libraries

This M3 prerequisite builds real AOSP Java implementations into DEX data. It
does not start ART, resolve native methods, package runtime resources or execute
a Java method. The output is not yet a usable ART boot classpath.

Use Python 3, an existing JDK 17 and a GCC or Clang C++ compiler:

```text
python -B scripts/build_art_classlib.py --java-home /path/to/jdk17 --cxx /path/to/clang++
python -B scripts/test_dex_loader.py --classlib-root build/m3/classlib
```

Both commands configure their temporary and cache paths through
`scripts/environment.py`. All paths are configurable. `ARTBOX_JAVA_HOME` can replace `--java-home`, followed by the
runner's JDK 17 environment variables and `JAVA_HOME`. Use `--build-dir` for
the output directory, `--jobs` for D8 concurrency and `--java-heap-mb` /
`--dex-heap-mb` for the two compiler heaps. Defaults are two jobs and 768/1024 MB.
The C++ compiler builds only Conscrypt's original constants generator.

Use `--fetch-jdk` instead of `--java-home` to download the pinned portable
Temurin 17 distribution into the configured cache. The installer verifies its
release size and SHA-256 before extraction and checks installed file hashes
before reuse. Complete distribution licenses remain with the tool. This option
supports macOS/Linux ARM64 and x64, plus Windows x64; other hosts can supply an
existing JDK. CI selects this option because its default JDK may change.

The verifier command executes on native macOS and Linux. macOS also builds
and ordinarily signs an iOS 15 framework containing the same format checks.
Windows compiles its Android ARM64 source units and records that native
execution was not tested. It does not emulate the CPU.

## Source and build contract

All eight source/tool components use exact commits from `android-15.0.0_r1`.
`third_party/sources.json` pins 3,349 selected files and the archive transports;
`third_party/art/classlib.json` records the Java source groups, generated
constants and expected output counts. The selected Java implementation includes
libcore/OpenJDK, ICU, Conscrypt and repackaged OkHttp/Okio plus their annotations.
See [third-party licenses](../THIRD_PARTY.md#m3-class-library-build-inputs).

Javac uses `--system none`, an explicit patched `java.base`, empty class/source
paths and disabled annotation processing. Host JDK implementation classes do
not become input. Its `-XDstringConcat=inline` option follows the pinned AOSP
Soong setting, avoiding an unavailable StringConcatFactory dependency without
editing Java source. D8 receives implementation classes, excluding module-info,
and Android API 31, matching libcore's minimum API. This is separate from the
iOS deployment target. The R8 jar is verified before execution.

Conscrypt's upstream C++ generator provides 50 integer constants using the
pinned BoringSSL headers. An ARTBox generator provides only ICU's two annotation
string keys from the checked aconfig input. It supplies no runtime flag API.
Original sources, generated inputs and their hashes are retained.

Each build uses a fresh `attempt-*` directory and retains failed diagnostics.
The output `artbox-classlib.zip` includes implementation DEX, the build report,
license notices and `corresponding-source.zip`. The latter contains all selected
source, original build declarations, ARTBox build commands/configuration and
generated inputs. Extract it to inspect the exact sources used; the embedded
`artbox` directory contains the portable command and pin manifest for rebuilding
with a supplied compiler and either a supplied or pinned portable JDK. R8 is fetched by its recorded hash.
JDK and R8 tool binaries are not included in the output. Preserve the source
bundle and notices when redistributing the implementation DEX.

## Checks and current evidence

The local portable build compiles **3,227 Java inputs into 6,422 class files**.
D8 emits two DEX039 files with **7,309 class definitions**, including synthetic
classes. The primary DEX is 8,547,244 bytes with 7,275 classes; the secondary is
65,664 bytes with 34 classes. A local run took 143.047 seconds for javac and
41.969 seconds for D8. These are workstation build times, not runtime performance.

The build checks file size, DEX header, SHA-1 signature, Adler-32 checksum,
required input classes and output counts. The AOSP verifier fixture additionally
checks both DEX files, uniqueness across them, ten required implementation
types and Object's private transient `shadow$_klass_` / `shadow$_monitor_`
fields. That metadata check does not prove an allocated ART object's layout.
Negative cases cover a corrupt checksum, an out-of-range class index with
corrected checksums, a duplicate class set and a missing secondary DEX.
Native CI results for this new fixture are pending.

The next integration steps are a linked ART runtime, native method libraries,
runtime resources, heap/reference adaptations and checked interpreter-only
startup. No JIT, executable DEX mapping or physical device is needed for these
format/build checks.
