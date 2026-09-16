# ARTBox status

M0-M2 are merged and tagged; automated acceptance passes. The project targets ordinary signed arm64
**iOS 15+** apps. Physical checks are waived as milestone gates; physical iPhone
execution remains unverified. Runtime milestones take priority over launcher UI.

## What runs

- M0: the portable startup path and independent iOS console print `ARTBox ready`.
- M1: an original static NDK ARM64 hello runs natively on macOS through both
  signed packaging prototypes, with five translated syscalls and exit zero.
- M2: real pinned AOSP Bionic starts with Scudo and GWP-ASan. A dynamically linked
  NDK suite passes **328/328 expectations (100%)** in both normal and forced
  sampling processes. The manifest loader links four signed libraries, resolves
  versions, relocates data atomically, and runs three constructors.
- Six real Bionic pthreads exercise concurrent allocation, files, mutexes,
  conditions, timed waits, errno/key isolation, return/exit and cleanup. Two ELF
  TLS templates preserve initialized, zero-filled and aligned storage across
  seven threads through Bionic's DTV and a precompiled TLSDESC bridge.
- Rooted regular files, anonymous/file mappings, virtual devices and initial
  `/proc/self/cmdline` work within their documented Linux-compatible subsets.
  Original NDK callers and Bionic syscall entries have native Linux comparisons.
- Portable CTest contracts pass on Windows, macOS and Linux. The integrated
  M2 iOS 15 IPA embeds the same diagnostic runner and four tested libraries,
  alongside the M1 fixtures. Its seven Mach-O images, original ELF resources,
  signatures, notices and source provenance are verified.
- M3 reference: original AOSP ART executes the hello DEX on native Linux ARM64
  through the switch interpreter with JIT and profiling caches disabled.

See [M2 acceptance](acceptance/m2.md) for exact revisions, CI links, artifact
hashes, counts and limitations. One traced Mac process takes 17.503 ms for
startup and clients, including 2.898 ms for the threaded workload. Forced GWP
sampling takes 19.755 ms and observes all 192 worker mallocs. Peak process RSS
is 6,717,440 / 6,488,064 bytes. These are correctness observations including
timed waits, not steady-state performance or iPhone measurements.

## What does not run

ART runs the hello DEX only in the native Linux reference. ART execution on
macOS/iOS, Binder/services, Android Activities and APK execution remain incomplete.
The M2 runner is diagnostic and single-use; unexpected contract failures can
terminate its process. General dlopen scope growth, unloading/finalization,
other ELF TLS models, signal delivery, broader proc files, mutable directories
and additional syscall families remain unsupported. The Windows native file
provider is not implemented; portable VFS tests use an injected provider there.

## In progress: M3 ART bring-up

At `7d0ed24`, original AOSP ART starts on native Linux ARM64, executes
`artbox.Hello.message()` from the original 448-byte DEX and returns
`hello from ARTBox ART`. The harness exits zero. Both
[host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35072533465) and
[iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35072533366)
pass. This is the first successful ART execution checkpoint; the current iOS
build contains earlier diagnostic fixtures and does not execute ART.

The runtime uses the C++ switch interpreter, semispace GC and imageless startup.
JIT compilation and profiling caches are disabled and checked before and after
the method call. Eleven native tests verify the executable-memory/process-execution
denial filter. Both compiler stack-initialization controls pass on native ARM64.
The same filter remains active during actual VM startup and method execution.
Downloaded libraries, DEX, source bundles, object hashes and mapping records
verify against their build records. See [startup evidence](m3-runtime-startup.md).

VM startup takes 60.884 ms; the full process takes 115.745 ms, including method
invocation and shutdown. These are single-run correctness timings, not interpreter
throughput or iPhone measurements. The process maps 934,653,952 bytes of virtual
address space after the method; this does not measure resident memory. No writable
executable mapping appears before or after execution. Time-zone data is incomplete,
and the successful invocation still logs an attached-thread warning at shutdown.
Those paths remain visible follow-up work.

The build selects 458 ART/support units, 480 nativehelper/ICU units and 209 native
libcore/bridge units. Eight ICU cases and 21 native libcore cases pass. The
implementation class library compiles 3,227 pinned Java inputs to two DEX files
with 7,309 classes. Original sources and notices accompany the outputs. The first
bootstrap failure exposed two omitted AOSP build settings: D8 desugaring and
zero initialization of automatic storage. Restoring those settings lets the
original class linker pass its retained class-layout checks (ADRs 0044-0045).

Earlier Apple probes remain prerequisites: [heap-relative reference checks](m3-references.md)
pass 38 native cases on each Mac/Linux host; [AOSP DEX validation](m3-dex.md),
[class-library validation](m3-classlib.md) and [runtime-policy fixtures](m3-runtime-policy.md)
also have native and signed iOS build evidence. These probes do not establish
full ART's managed ABI, garbage collection or JNI behavior on Apple platforms.
The [runtime build](m3-runtime-build.md), [native dependencies](m3-native-libraries.md)
and [libcore build](m3-libcore-native.md) record their source and ABI boundaries.

The next [managed acceptance fixture](m3-managed-checks.md) tests allocation,
cyclic references, explicit null/bounds exceptions, virtual dispatch, verified
collection and repeated native-thread attachment. Its portable producer and
extended JNI harness are prepared; native ART validation is pending. Then adapt
the full managed reference/stack representation and thread/signal
boundaries, and integrate the same runtime and DEX into signed macOS/iOS builds.
The [M3 contract](m3-contract.md) remains unmet until those execution and shared
iOS requirements pass. AOT/OAT execution and host dex2oat are still future work;
M3 permits interpreter-only acceptance. M4-M7 follow M3 acceptance.

The three largest M3 risks are the complete managed ABI above Apple's 4 GiB guard,
thread suspension/signals and shutdown across native boundaries, and iOS memory
limits. The existing M2 Apple Bionic runner reserves about 8.25 GiB of virtual
address space; combining it with ART has not been validated. Neither host virtual
mapping totals nor build success establish an iPhone memory budget.
