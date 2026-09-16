# Managed ART acceptance checks

The original hello DEX remains the first method invocation. The additional
`RuntimeChecks.java` fixture exercises allocation, cyclic object references,
array contents, virtual dispatch and explicit null/bounds exceptions through ART.
It requests collection with `Runtime.getRuntime().gc()`: the pinned Android
`System.gc()` can defer collection for target SDK versions at or below 34.

The native harness reads the original
`dalvik.system.VMDebug.getRuntimeStat("art.gc.gc-count")` before and after the
heap check. The counter must increase, and the managed code must preserve its
rooted graph and object identity across the call. A GC request alone is not
collection evidence. The checksum must be 6496, and the exception mask must be 3.

Two native threads each attach to ART and detach twice. They check `GetEnv`
before attachment, after attachment and after detachment. Each invokes a
synchronized Java method through a global class reference, verifies its managed
thread name and updates a counter. Both threads must complete; the count must
equal four. The main invocation checks VM registration, detaches before VM
destruction and checks that no VM remains registered afterward. Mapping snapshots
cover entry, completed managed calls and shutdown. The executable-memory denial
filter remains active for the entire process.

The harness also reports ART's current allocated managed bytes and its own Linux
process peak RSS through `getrusage(RUSAGE_SELF)`. Linux reports that peak in KiB;
see the [Linux interface documentation](https://man7.org/linux/man-pages/man2/getrusage.2.html).
It does not use the Python parent's child-process statistics, which also include
compiler processes. Managed allocation is an observation that can change during
collection; the process RSS includes native libraries and other mappings. Neither
is an iPhone memory measurement.

Build the test input with a configured JDK 17, or fetch the same pinned portable
JDK used by the class-library build:

```console
python -B scripts/build_art_managed_fixture.py --fetch-jdk
```

The builder works on Windows, macOS and Linux. It uses the pinned D8 compiler,
keeps scratch files in the configured build directory and emits a separate
`artbox-runtime-checks.zip`. The archive contains the DEX, its original class
files, source, notices and a build report. Before ART sees the DEX, the startup
driver checks the artifact hash, complete project-source hashes, DEX envelope
and checkout revision. It preserves the test DEX and corresponding source with
the execution attempt. The boot class path and original hello DEX are unchanged.

A host JDK driver checks fixture logic before conversion; it is excluded from
the DEX payload. Host execution, D8 output and ARM64 compilation do not prove ART
execution. Native Linux execution of the extended checks is pending; Apple ART
integration and the remaining [M3 contract](m3-contract.md) are also incomplete.
