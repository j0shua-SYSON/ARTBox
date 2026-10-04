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
  versions, relocates data atomically, and runs the selected Bionic constructors.
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
- M3 native bootstrap: all 31 constructors in the signed four-library ART group
  return on Mac ARM64 at `d43ceaf`. Pre-start JNI registration, shared heap
  binding and resource cleanup pass. This does not start a JavaVM or execute DEX.
- M3 native class libraries: the signed 15-image group at `c3fd5aa` returns from
  38 constructors, passes all 17 native groups and 228 integer-division vectors,
  joins/reaps its monitor worker and releases resources. JNI_OnLoad and JavaVM
  startup are not invoked.

See [M2 acceptance](acceptance/m2.md) for exact revisions, CI links, artifact
hashes, counts and limitations. One traced Mac process takes 17.503 ms for
startup and clients, including 2.898 ms for the threaded workload. Forced GWP
sampling takes 19.755 ms and observes all 192 worker mallocs. Peak process RSS
is 6,717,440 / 6,488,064 bytes. These are correctness observations including
timed waits, not steady-state performance or iPhone measurements.

## What does not run

ART runs the hello DEX only in the native Linux reference. JavaVM startup and DEX
execution on macOS/iOS, Binder/services, Android Activities and APK execution remain incomplete.
The M2 runner is diagnostic and single-use; unexpected contract failures can
terminate its process. General dlopen scope growth, unloading/finalization,
other ELF TLS models, asynchronous signal delivery, broader proc files, mutable directories
and additional syscall families remain unsupported. The Windows native file
provider is not implemented; portable VFS tests use an injected provider there.

## In progress: M3 ART bring-up

At `1647514`, the dependency selection adds 57 unchanged Bionic units and AOSP-generated
Android account IDs. New callers cover numeric address resolution, bounded
strings and Android account storage; they fail to link against the previous
libc as expected. The 338-unit build passes all 75 cases in both signed Mac
modes; native Linux passes the 45 common string/resolver cases. All 16 original
ID-generator tests pass on Mac. Both complete CI workflows are green.
DNS/network services, interface discovery and the other newly
imported syscall families remain outside the implemented kernel subset.

The five Android native class libraries and their caller now execute on Mac
ARM64 through signed frameworks at `c3fd5aa`. Independent artifact checks
verify 208 original objects plus the caller, 1,666 upstream inputs, 131,813
instructions, twelve Mac/iOS framework layouts and 204 notice hashes. The
17-group native call takes 0.577 ms and 228 integer vectors take 0.039 ms in one
traced correctness run; load/relocation takes 48.201 ms and bootstrap 1.560 ms.
These are not steady-state or iPhone performance measurements.

This required rooted unlink with a separate 29-case lifetime/error contract
and removal of unnecessary JNI dependency edges. Both signed Mac allocator
modes and both native Linux profiles pass all 29 unlink cases. The original
JVM cleanup and hidden-cache assertions remain mandatory.

Next is [signal registration and delivery](m3-signals.md), followed by actual
Apple JavaVM startup, JNI_OnLoad and DEX acceptance. The Linux signal reference
passes 70 checks at `7921eff`, including real alternate-stack handler execution
and a worker-wakeup negative control. Portable queues now implement blocked
thread-directed signals and synchronous waits, with clone/reaper lifetime
integration. At `9c6a22f`, both signed Bionic modes and the identical NDK object
on native Linux pass the 33-case contract; source/object hashes and logs verify
independently. Both complete CI workflows pass, including all 17 host jobs.
M2 retains its fixed 328/328 score. The integrated iOS 15 IPA verifies seven
Mach-O images and empty entitlements; it still contains M1/M2 diagnostics.

The next signal-context codec encodes Linux ARM64 handler frames and extracts
PC/SP/general/SIMD edits while preserving Apple's reserved x18 and non-NZCV
PSTATE bits. All 27 local core tests pass; at `c687998`, a real Linux signal-return
test resumes edited PC/x0/SIMD state and detects dropped edits. Its source,
binary and CTest log verify independently. At `c16c648`, the signed Darwin test
also resumes edited PC/x0/SIMD state, preserves x18 and detects dropped edits.
Independent checks verify six source hashes, the native executable and nine
CodeDirectory page hashes. Both complete CI workflows pass at that revision.
At `9200f91`, a real Mac fault while the mapper mutex is held uses a separate
syscall/TLS scope. Nested restoration, thread isolation and errno preservation
pass; both full CI workflows are green. Independent inspection verifies sixteen
source hashes, the executable and nineteen signed code pages; disassembly keeps
ordinary compiler-TLS resolution behind the signal-scope branch.

At `2aa064f`, Bionic `sigaction` registers a signed Android SIGTRAP handler on
ARM64 Mac. Both allocator modes pass 16 checks, including Linux siginfo/context,
Bionic TLS/errno, mask query and PC/x0/SIMD return; dropped register edits fail
as intended. The same source passes native Linux. Independent source/object,
binary, stdout and framework checks verify this result. All 17 host CI jobs and
the iOS build pass. The integrated 1,138,166-byte diagnostic IPA verifies seven
iOS 15 images and empty entitlements; it still does not contain ART.

At `e7e1647`, alternate-stack registration and delivery pass on signed ARM64 Mac
and native Linux: 17 shared wire cases, 24 handler/worker checks, a near-guard SP
and a missing-SA_ONSTACK control. Both Bionic modes and the identical Linux wire
object pass. All 17 host CI jobs and the iOS build are green. The independently
verified 1,145,107-byte diagnostic IPA has seven iOS 15 images, four ELF layouts,
32 notices and empty entitlements. It contains no ART runtime. Compiled snapshots
have no function calls; the signed stack bridge restores host SP and preserves x18.

At `e8408fd`, mask/pending transitions are coherent so a handler can change
its mask while another thread queues a signal. Local tests pass 4,096 enqueue/
unmask races and exercise the handler API while the mapper lock is held. Apple
ARM64 uses lock-free 128-bit transitions; other targets retain ordinary-context
support without advertising handler mutation. The new signed/native Linux
caller tests action masks, handler mask changes, stable image buffers and an
edited return mask that preserves a queued signal. All 18 handler checks and the
omitted-unblock control pass on native Linux and both signed Bionic modes. All
17 host jobs and the iOS build pass. Downloaded sources, objects, native results
and the 1,148,066-byte diagnostic IPA verify independently. Compiled iOS mask
updates tail-call a local helper containing exclusive-pair loops, with no
library calls. No physical execution or Apple JavaVM is claimed.

At `886d1bb`, delivery-owner flag probing and a real pinned AOSP
sigchain caller pass native acceptance. The caller invokes libart's named sigaction/sigprocmask wrappers,
registers a special handler, tests accepted and forwarded faults on an alternate
stack, removes that handler and verifies ordinary user forwarding. It checks
the scoped handling TLS bit through actual mask behavior. A removed-handler
control fails as intended. Thirty portable tests pass, and all 22 sigchain
assertions pass on native Linux and the signed ARM64 Mac bootstrap. All 17 host
jobs and the iOS build pass. Source/binary/framework checks verify independently:
463 guest objects, 207 project sources and 44 runtime-framework notices. The
1,148,514-byte integrated diagnostic IPA verifies seven iOS 15 images and empty
entitlements; it still contains M1/M2 diagnostics rather than ART. One Mac sample
reports 30.943 ms for load/relocation and 0.805 ms for constructor/acceptance
execution; these are not JavaVM startup or interpreter performance measurements.
The first signed run exposed a test expectation error: Bionic forces its reserved
POSIX-timer bit into sigprocmask, unlike glibc. All eleven recorded masks and the
assertion failure bitmap match that difference exactly at `87f866f`. The caller
now checks the complete Bionic mask using its pinned timer-signal constant;
the runtime and Bionic implementation are unchanged by this correction.
Other fault signals and unblocked queued delivery remain incomplete.
JavaVM/JNI/DEX execution on Apple is still required.

At `f6251fd`, the native reference verifies Linux/Darwin metadata for null access,
protected-page reads, read-only-page writes, unaligned atomic loads and undefined
instructions. It verifies alternate-stack delivery, fault PC/address, return
register edits and x18 preservation, with dropped-edit/address controls. Its
five cases and both mutation controls pass on native Linux and signed ARM64 Mac.
Source/binary hashes and thirteen Mac signature pages verify independently.
All seventeen host jobs and the iOS workflow pass. Darwin reports protected
memory access as SIGBUS where Linux reports SIGSEGV; even their matching ARM
translation-fault syndromes cannot distinguish a mapping hole from PROT_NONE.

The next bridge uses portable VM metadata to deliver Linux MAPERR/ACCERR,
alignment BUS_ADRALN and UDF ILL_ILLOPC to Android handlers. Its read-only query
uses lock-free counters without entering the VM mutex, allocating or waiting
for a writer. Concurrent mapping mutation returns EAGAIN; file EOF/I/O faults,
MTE, nested synchronous faults and other unmeasured cases remain unsupported.
Thirty-two local portable tests pass, including 1,024 metadata-reader races and
classification error cases. The Android caller compiles with no raw syscall,
direct TLS or reserved-register instructions. Five delivered faults, mask/errno
preservation and two mutation controls are now required in both signed Bionic
modes and against the same source on Linux; their native CI execution is pending.
This does not yet start an Apple JavaVM or complete M3.

The native-class-library dependency set now includes the pinned AOSP vfork frontend.
Its native overlay keeps Bionic's state/errno logic while routing TLS and the
raw clone request through existing bridges; process creation remains ENOSYS.
At `3826391`, 28 captured-reply cases and two real guest rejection checks pass
in both signed Mac modes. Native Linux also passes the 28 cases and rejects
the deliberate register-preservation mutation. The complete
[host workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36302395868)
and [iOS workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36302395806)
pass, including both ART interpreter references and signed ICU execution.
Independent downloads verify the shared source/object hashes, eight Bionic
framework layouts and the 906,141-byte diagnostic IPA. This does not start a
JavaVM on Apple or change M2's fixed 328-expectation denominator.

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
and the adapted iOS 15 framework is verified. The full guest link includes that
reviewed restore object.
The [source-built math dependency](m3-math.md) now selects the 20 math imports
needed by ART, with a separate 78-case dynamic client. All cases pass on signed
Mac code, the identical Android libraries on Linux, and Linux system libm at
`236625c`. Downloaded sources, objects, ELF files and four signed framework
layouts verify; the full Apple runtime still needs its remaining Bionic and loader APIs.
The [Bionic dependency expansion](m3-bionic-dependencies.md) adds 50 unchanged
source units and a separate 30-case libc client. Both signed Mac modes and
native Linux pass at `a3e46d6`, with the M2 scores unchanged at 328/328.
The complete workflows and downloaded iOS 15 diagnostic IPA verify. Compiled
wrappers do not imply new kernel-service support; the IPA still contains no ART.
All 480 Android ICU/JNI units now compile locally. A diagnostic link identifies
six additional math/libc imports. Expanded 108-case math and 42-case libc clients
reproduce their missing-symbol failures before the unchanged AOSP implementations
are added. At `4abb7b1`, all 108 math vectors and 42 libc checks pass through
signed Bionic on Mac ARM64 and their native Linux references. Downloaded source,
object, framework and notice checks pass; both M2 modes remain at 328/328.
The updated local link resolves all strong imports of the five ICU/JNI libraries
against the guest dependency set. This diagnostic uses local objects and earlier
verified inputs; it is not yet a signed or executed ICU artifact.
The public ICU pipeline now requires all producers to share a Git revision,
checks the actual dependency closure and packages five libraries as Mac/iOS 15
frameworks. At `d68dd3e`, signing and the complete host/iOS workflows pass,
including both Linux ART startup profiles. At `ecf9000`, a separate guest image
passes the original eight ICU groups through the shared ten-image Mac runtime.
It loads pinned data through the rooted filesystem and releases ICU data/caches
before teardown, without invoking JNI_OnLoad. All 33 constructors return.
Independent checks verify 480 upstream objects plus the caller, twelve signed
framework layouts and the execution report. The ICU check takes 1.381 ms in one
Mac run; see [scope and timings](m3-native-libraries.md). Apple JavaVM/DEX
acceptance remains incomplete, and the integrated IPA is still the M1/M2 diagnostic.
Source review identified ICU's required `MADV_RANDOM` call. Anonymous/file
regressions reproduce its rejection; the portable implementation now accepts
the nonbinding hint after validation. All 25 local tests and the native Linux
ARM64 comparisons pass at `ecf9000`. Read-ahead tuning remains unchanged.
All 208 Android native libcore units compile in a local diagnostic. Its link
identifies further libc/compiler-runtime dependencies; that result does not
establish signed execution. The next dependency expansion adds six unchanged
libc units and two libm units, with 73-case libc and 130-vector math callers.
Both callers reproduce the missing-symbol failures against the previous
libraries before the source selection changes. Their new native execution
checks pass at `030b310`: both signed Mac allocator modes and native Linux libc
pass all 73 checks; signed Mac, Android ELF on Linux and system libm pass all
130 vectors. Downloaded artifacts independently verify the sources, objects,
layouts, notices and the integrated iOS 15 diagnostic IPA. Both complete
[host](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36300511614) and
[iOS](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36300511621) workflows pass.
OpenJDK's error-string unit now selects Bionic's POSIX declaration during Android
compilation. A 22nd native libcore test group passes in both Linux profiles;
Android libcore execution through the signed Apple runtime remains pending.
Apple JavaVM startup remains the M3 acceptance target.
The [loader service queries](m3-loader-services.md) now cover named dependency
scope, lookup after the caller and ELF address/image metadata. All 25 local
host tests pass. The portable handle/error context also passes concurrent tests,
and a native Linux reference passes 32 loader calls and six thread-error checks.
The Android loader API bridge now validates guest pointers, marshals metadata
and owns per-thread error storage. Its unchanged AOSP libdl frontend passes the
32-case/six-thread-error fixture in signed Mac execution at `10eb3b6`. Downloaded
sources, objects, ELF files, six Mac/iOS framework layouts and notices verify.
Full Apple ART startup remains pending.
The full Android guest object build now has an explicit `--native-guest` profile:
462 units share the host-owned VM and use the reviewed Bionic/HeapSampler TLS
boundaries. All units compile in macOS CI at `44008be`; source, object, generated
input and notice hashes verify independently. Both full Linux startup profiles
and the complete workflows pass. The full-runtime link passes the instruction
and import checks with eight intentional VM/TLS host imports. Signed Mac/iOS 15
framework layouts and empty entitlements pass in CI at `5b570af`. The shared
Bionic loader/TLS and VM services now initialize Bionic and enter ART
constructors on the Mac CPU. At `d43ceaf`, all 31 constructors, pre-start JNI,
shared heap binding and cleanup pass. Its virtual `uname` implementation passes
native Linux comparisons; 16 injected cases verify the narrow adaptation for an
unsupported memory-probe prerequisite. The independently verified bootstrap takes
24.151 ms to load/relocate and 0.709 ms for the constructor/JNI/heap phase, including
thread setup and join. These are single-run Mac observations. See
[bootstrap evidence](m3-native-bootstrap.md). JavaVM startup and DEX execution
remain incomplete, and the iOS app still contains no ART runtime.
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
