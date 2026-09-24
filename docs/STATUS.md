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
  through the switch interpreter with JIT and profiling caches disabled. Its
  managed graph survives explicit collection; exceptions and native-thread
  attachment, detachment and VM shutdown checks pass.
- M3 high heap: the adapted full runtime also passes that suite on native Linux
  ARM64 at `71f398e`, with checked references and an owned 4 GiB heap window.
  This validates managed allocation and collection before Apple ABI integration.

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

At `7f9d8da`, original AOSP ART starts on native Linux ARM64, executes
`artbox.Hello.message()` from the original 448-byte DEX and returns
`hello from ARTBox ART`. The extended managed acceptance checks pass and the
harness exits zero. Both
[host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35077793094) and
[iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35077793076)
pass. The first successful hello was at `7d0ed24`; the current iOS
build contains earlier diagnostic fixtures and does not execute ART.

The runtime uses the C++ switch interpreter, semispace GC and imageless startup.
JIT compilation and profiling caches are disabled and checked before and after
the method call. Eleven native tests verify the executable-memory/process-execution
denial filter. Both compiler stack-initialization controls pass on native ARM64.
The same filter remains active during actual VM startup and method execution.
Downloaded libraries, DEX, source bundles, object hashes and mapping records
verify against their build records. See [startup evidence](m3-runtime-startup.md).

VM startup takes 65.999 ms; the full process takes 115.915 ms, including managed
checks and shutdown. Explicit semispace collection advances the GC counter from
zero to one and preserves the graph and dispatch checksum. Both expected exceptions
and four native-thread attachment cycles pass. Explicit main-thread detachment
removes the earlier attached-thread shutdown warning; VM registration is empty
after destruction.

Peak process RSS is 27,808 KiB, with 637,920 managed bytes allocated at the recorded
observation. The process maps 1,102,565,376 virtual bytes after the checks and
549,445,632 after VM destruction. These are single-run Linux correctness and memory
observations, not interpreter throughput or iPhone measurements. All 18 executable
mappings remain unchanged through shutdown, with no writable executable mapping.
Time-zone data is still incomplete. See [managed evidence](m3-managed-checks.md).

The build now selects 459 ART/support/test units, 480 nativehelper/ICU units and 209 native
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

The [managed acceptance fixture](m3-managed-checks.md) passes on both original
and high-heap native Linux runtimes. Its macOS and Linux producers emit identical
DEX bytes; downloaded payloads and corresponding project sources verify. The
direct thread-state regression passes both native Linux profiles at `8720bc8`: it checks
ART's current-thread/JNI relationship and the runtime's actual sampler TLS across
three simultaneous threads and four attach/detach cycles. Next adapt the native
thread/signal boundaries and integrate the runtime into signed macOS/iOS builds.
The [LLVM context boundary](m3-unwind-context.md) passes four checks on signed Mac
and native Linux at `bba15cd`; the original Linux control changes the reserved
register as expected. Both source-built objects match their NDK counterparts,
and the adapted iOS 15 framework is verified. The full guest runtime is not linked yet.
The [managed-storage contract](m3-managed-storage.md) now passes 54 cases on each
native Mac/Linux host, with two signed Mac controls confirming the original
forwarding-address truncation. GC forwarding words and JNI reference/free/serial/
dead-entry representations pass above 4 GiB with the checked codec. Downloaded
sources, binaries and signed iOS 15 framework build artifacts verify. Full ART
moving collection now also passes in the Linux high heap; Apple integration
remains pending.
The [interpreter argument-copy contract](m3-interpreter-arguments.md) passes 36
cases on each native Mac/Linux host. Both partial-adaptation controls lose the
callee's reference slot at the expected case; the checked comparison preserves
it. Actual AOSP ShadowFrame/copying helpers and downloaded source/binaries verify.
The portable mapper's [stable managed window](m3-heap-window.md) passes native
Mac/Linux/Windows CI at `3ad7f50`, including guards, reuse and the 4 GiB boundary.
The extension attaching windows to the same registry as Bionic syscall buffers
passes full native CI at `a700398`. Ordinary mmap
stays outside the managed pool, while guards and holes fail syscall validation.
The [full high-heap runtime profile](m3-high-heap-runtime.md) now connects ART's
actual MemMap and card table. At `0979faf`, all 463 runtime/support/test units
build and the MemMap/CardTable preflight passes on native Linux ARM64. Startup
then aborts in the checked encoder during class-table lookup: that slot still
stores truncated absolute pointers. At `5acd098`, the actual AOSP slot regression
passes for all hash tags, copies and root updates. The high-heap runtime starts
in 77.128 ms and executes the real hello DEX, then fails its explicit GC check
because the large-object bitmaps still describe the low 4 GiB. At `71f398e`,
their extent adaptation and constructor/bit-operation regression pass, and the
full high-heap suite passes GC, exceptions, four attachment cycles and shutdown.
All required host and iOS build CI is green. Downloaded build/startup payloads,
sources and unchanged executable mappings verify. High-heap startup takes
79.516 ms with peak RSS 32,624 KiB in this one Linux correctness run; see the
linked profile for the paired original-runtime observations and their limits.
The original runtime remains separately required. Apple ART execution still
needs native ABI/TLS, signal, dependency and signed-package integration.
The [M3 contract](m3-contract.md) remains unmet until those execution and shared
iOS requirements pass. AOT/OAT execution and host dex2oat are still future work;
M3 permits interpreter-only acceptance. M4-M7 follow M3 acceptance.

The three largest M3 risks are the complete managed ABI above Apple's 4 GiB guard,
thread suspension/signals and shutdown across native boundaries, and iOS memory
limits. The existing M2 Apple Bionic runner reserves about 8.25 GiB of virtual
address space; combining it with ART has not been validated. Neither host virtual
mapping totals nor build success establish an iPhone memory budget.
