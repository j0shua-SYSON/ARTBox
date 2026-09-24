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

The native fixture also checks ART's actual current-thread pointer and heap
sampler TLS. A helper compiled inside `libart` reads both: instantiating the
inline sampler accessor in the harness could create a separate hidden TLS
variable and test the wrong storage. With sampling disabled, the main thread
and both workers place distinct temporary values in their own sampler slots.
The workers rendezvous before attachment so the main thread can compare three
live, distinct slots. Each worker requires null ART thread state before attach,
the matching JNI environment after attach, and null state after detach, twice.
Its TLS address and value must survive those cycles independently of the main
thread. Original sampler values are restored before the workers return and
before the main thread continues.

Both runtime profiles require a separate `ARTBox thread state` result containing
three isolated TLS slots and four attachment cycles. Local ARM64 compilation
passes; native execution of this additional regression is pending. The earlier
verified runs below predate this direct thread-state check.

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
execution. Apple ART integration and the remaining [M3 contract](m3-contract.md)
are incomplete.

## Verified native execution at 7f9d8da

[Host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35077793094) and
[iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35077793076)
pass. The native Linux ARM64 run executes the original hello, then reports:

```text
ARTBox: managed heap checksum 6496; collections 0 -> 1
ARTBox: managed null and bounds exceptions passed
ARTBox managed checks: {"heap_checksum":6496,"exceptions":3,"gc_before":0,"gc_after":1,"attachments":4}
ARTBox runtime memory: {"managed_allocated_bytes":637920,"process_peak_rss_kib":27808}
ARTBox: native ART lifecycle checks passed
```

The process exits zero. Original ART logs an explicit semispace collection:
308 KB freed, 839 microseconds total and 798 microseconds paused. The retained
graph, arrays and virtual dispatch survive that collection. VM startup takes
65.999 ms; the full process takes 115.915 ms. These are correctness observations
from one Linux process, not throughput benchmarks or iPhone measurements.

The launching thread detaches before VM destruction, and the earlier
attached-thread shutdown warning is absent. All eleven code-generation guard
cases pass. All 18 executable mappings are identical before startup, after
managed checks and after VM destruction; no writable executable mapping appears.
Total virtual mappings are 1,102,565,376 bytes after the checks and 549,445,632
after destruction. Those totals are distinct from the measured peak RSS above.
Missing boot-image fallback, optional DEX metadata and incomplete time-zone data
remain visible in the log.

The startup artifact has SHA-256
`c6a9a7e4ed88d2944368bc3a09af40184a8e1c60eefcc1a357106d69c7e1a8c1`.
Its 18 retained payload hashes verify. The harness and eleven shared libraries
are ARM64 ELF with no writable executable load segment or executable stack.
The source bundles contain 84 distinct project files matching the canonical
commit bytes. The tested merge `48cde648aa36625bfed6da0c124e549e017ba858`
contains branch head `7f9d8dacc1b8a02d5270157a030f9f27c768b51f`.

Both macOS and Linux fixture producers emit the same 3,148-byte DEX 039 with four
classes and twenty methods, SHA-256
`b6d6294c020dad8f08549cf0083de0ebe122f9825786209580a716ef983cafa9`.
Both producer bundles, their three payload hashes, fourteen project-source
hashes and corresponding-source contents verify. The macOS producer executes
the host JDK logic check; it does not run ART.
