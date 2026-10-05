# ARTBox status

M0-M3 are merged and tagged. **Real AOSP ART executes hello DEX on the test
iPhone and through signed frameworks on macOS ARM64**, with collection,
exceptions, native-thread attachment and shutdown verified. Target: ordinary
signed arm64 **iOS 15+**, no JIT or private entitlements. Physical checks remain
optional. M0-M3 pass on an already jailbroken iPhone 6s Plus running iOS 15.8.5,
including background/foreground and cold home-icon relaunch without a debugger.
Ordinary stock-device provisioning remains unverified.

## What runs

- M0: portable startup and the independent iOS console print `ARTBox ready`.
- M1: a static NDK ARM64 hello runs on Mac and the test iPhone through both
  signed packaging prototypes, with five translated syscalls and exit zero.
  Home-icon launch, background/foreground and cold relaunch pass on the phone.
- M2: pinned AOSP Bionic, Scudo and GWP-ASan pass **328/328** expectations in
  both normal and forced-sampling processes. Six Bionic pthreads exercise
  allocation, rooted files, mappings, synchronization, TLS, errno and cleanup.
  The integrated diagnostic IPA includes the same four signed libraries. The
  normal profile also passes all 328 checks and three allocator-pressure checks
  on the test iPhone at `31c9a4a`, within a 1 GiB VM reservation budget.
- M3 Linux references: both original and high-heap AOSP profiles execute hello,
  explicit GC, exceptions, attachment cycles and VM shutdown with JIT disabled.
- M3 signed Mac runtime at `68400fe`: 15 native images, 38 constructors, actual
  JavaVM creation, exact hello output, a managed graph surviving collection,
  three exceptions, four attachment cycles, thread-state isolation, shutdown
  and eight reaped threads. A missing-hello process fails class lookup as required.
- M3 iPhone runtime at `2970c38`: the same hello and managed checks pass on the
  device. JavaVM creation takes 198.664 and 247.610 ms in two traced correctness
  runs, with about 101 MiB peak RSS and 1,095,008,256 reserved guest bytes. All
  16 installed code images match the independently verified CI IPA.
- Kernel extensions: virtual getcwd and queued signal-34 interruption pass
  signed Bionic and native Linux. Private futex requeue moves actual waiters,
  preserving masks, deadlines and signal ownership. Its 18-case identical NDK
  object passes both signed Bionic modes and both Linux syscall paths; C++ tests
  compare blocked-waiter behavior and 128 races against the Linux kernel.

At merged M3 head `8cf6315`, all 18 host jobs and the iOS build pass on both the
PR and main, including console controls and the integrated ART IPA. The 34 M3 Windows contracts,
36 native Linux ARM64 contracts, downloaded source/object hashes, signed
framework layouts and diagnostic IPA verify independently. See
[M3 acceptance](acceptance/m3.md), [M2 acceptance](acceptance/m2.md),
[syscall subsets](syscalls.md) and [architecture decisions](DECISIONS.md).

## What does not run

Hardware bring-up exposed oversized Scudo/ART reservations and an optional CRC
instruction emitted without feature dispatch. The bounded Scudo primary,
software-checksum path and 512 MiB managed arena resolve these failures on the
test phone without changing the 128 MiB Java maximum or managed acceptance.
ADRs 0126-0128 and the [M2](acceptance/m2.md) / [M3](acceptance/m3.md) evidence
retain the failed candidates, successful runs and measurements. All 22 host jobs
and iOS pass at `2970c38`; its integrated ART IPA independently verifies 16
Mach-O images, 15 ELF layouts, five resources, 175 notices and empty entitlements.
Ordinary provisioning, larger app heaps and sustained interpreter performance
remain unverified.
Binder/services, Activities, AndroidX APKs, graphics and audio remain future
milestones. The runtime is a one-shot diagnostic; repeated VM creation and
general dynamic library growth/unloading are not supported. AOT/OAT and host
dex2oat remain future coverage under interpreter-only M3 acceptance.

Additional syscall families, broader process and signal behavior, mutable cwd,
directory mutation and broader proc metadata remain unsupported. The Windows
native file provider is absent; portable VFS tests inject a provider there.
Unsupported operations retain explicit errors. See the individual contracts.

## Current work

The original VINTF kernel-config parser now has one shared Android caller for
native Linux and signed Apple execution. Local object/audit and harness checks
pass; CI execution is pending. Its 89 assertions and two precise failure
controls exercise the original parser and hash-pinned libc++ regex member.
This closes a servicemanager dependency only after both execution gates pass;
real service registration, trusted caller policy and manager/client isolation
remain required for M4.

M4: the [Binder wire boundary](binder.md) recognizes the Android 15 ARM64
commands and validates transaction/object snapshots. Local checks pass 40 host
contracts and 60 comparisons against actual NDK UAPI constants plus an
independently produced byte fixture. A bounded receive-arena primitive also
passes local ownership/exhaustion checks and 8,000 concurrent lifecycles.
Its synchronous transaction integration passes at bf36338. At `4012961`, all 18 host jobs and iOS
CI pass, including actual native receive-alias coherence, protection-fault and
close/unmap tests on Mac/Linux. At `c773122`, the real Linux Binder device passes
the 30-case ioctl/lifetime reference; downloaded source hashes and run identity
verify. At `517ad1c`, Linux and the portable endpoint API pass the same 32-case
fixture; 87 input hashes and artifact provenance verify. A configured VFS now
routes `/dev/binder` open/ioctl/close to that API and passes the same local fixture,
a 22-case file contract, cleanup controls and 4,000 concurrent descriptor lifecycles.
At `ba255d2`, Linux and VFS pass the same 32 ioctl/22 file cases, with 90 input
hashes verified. At `4d3a124`, the native mapping/lifetime fixture passes 28
cases, with 92 input hashes verified; mapping comparison remains explicitly
false. Passive VM lifetime watches now pass local ownership/failure checks and
128 concurrent teardown cycles. The receive path now retains independent native
aliases and mapped endpoint ownership through descriptor close. Its 35-case shared
fixture and allocation, replacement and concurrent-close controls pass locally.
At `db73182`, Linux and ARTBox pass the same 35 mapping cases, including native
alias coherence, with artifact provenance and 93 input hashes verified. Polling
and real servicemanager remain ahead; M4 is not complete.

At `200b910`, the shared threaded transaction fixture passes against real Linux,
including same-PID context-manager rejection and a separate-process positive
client. Its artifact and 95 source hashes verify. At `bf36338`, the production
synchronous handle-zero byte-parcel path passes the same Linux fixture; artifact
provenance and all 96 source hashes verify. All 19 host jobs and the iOS build
pass. The downloaded signed Mac runtime also verifies all managed/console
controls and 96 source hashes.
At `1f9f107`, manager strong/weak references and death/clear/acknowledgment
delivery pass all three shared Linux/ARTBox lifetime cases; all 19 host jobs and
iOS pass. Downloaded runtime/IPA evidence independently verifies. At `1423d52`,
the native object fixture also passes handle retention, return-to-owner
translation, a callback transaction and object-owner death; 96 source hashes
verify, with `object_driver_compared` explicitly false at that checkpoint.
At `74a6c9c`, production strong-object translation and buffer-held references
pass the complete native Linux/ARTBox lifecycle plus three malformed-parcel
rollback cases. Artifact provenance and 96 input hashes independently verify.
All 19 host jobs and iOS pass. Downloaded runtime/IPA evidence independently
verifies the managed/console controls, 16 iOS 15 images, 15 ELF/layout pairs,
five runtime resources and 175 notice hashes with empty entitlements.

At `f8b21fb`, one-way delivery passes the shared Linux/ARTBox lifecycle: callbacks
inside synchronous handlers, two independent nodes, buffer-release ordering and
queued delivery after sender death. Artifact provenance and 96 input hashes
verify; all 19 host jobs and iOS pass. Local copy-fault and sender/receiver teardown
controls also pass. Weak objects, FD transfer, nested synchronous calls, blocking
reads and polling remain unsupported.

The real servicemanager dependency pipeline now pins its five AOSP AIDL inputs
and the native host compiler with source-manifest-linked notices. Windows verifies
preparation and cache integrity; required Mac/Linux CI generates the C++ bindings,
compares two generation directories and checks malformed-input rejection. Compiler
execution passes at `49e3786` on Mac ARM64 and Linux x86_64, with all 20 generated
files independently verified byte-identical across hosts. All 19 host jobs and
iOS pass; kernel comparison, signed Mac runtime and integrated IPA artifacts
independently verify at that head.

The [native Binder build](binder-build.md) now compiles the real Android kernel-IPC
libbinder profile and its libutils support into ARM64 archives, consuming those
verified generated bindings. The build records unresolved imports and retains
exact corresponding source and notices. At `9c4e6be`, all 20 host jobs and iOS
pass. The downloaded build independently verifies 49 objects, both archives,
331 upstream files, 14 project inputs and all 20 generated files. Compilation
takes 17.8 seconds on the Mac runner; disassembly confirms no x18/w18 use or
direct SVC instructions. The 176 external imports remain explicit.
Linking, real servicemanager execution, runtime attachment and M4 acceptance
remain required; producing archives does not satisfy those checks.

At `ba2cb96`, all 20 host jobs and iOS pass. The four native wait cases verify
empty-read interruption, interruption after consuming ENTER_LOOPER, invalid
output and nonblocking empty read. The fixture verifies the initial thread NOOP,
then observes the actual blocked ioctl before signaling. Its artifact and 97
source hashes verify; `wait_driver_compared` is false at that checkpoint.

Production blocking reads now release the device mutex, retain the open
operation across descriptor close, and observe delivered-interrupt epochs.
Local tests cover consumed writes, thread-vector changes, close/reuse and death
notification after passive final unmap. All 40 Windows host contracts pass;
three native-backing contracts remain unavailable there. At `e53e803`, both
blocking and nonblocking ping/reply and all four wait
contracts pass Linux/ARTBox comparison. The downloaded artifact and 97 source
hashes verify; ARTBox's interrupt epoch is injected, not native signal delivery.
All 20 host jobs and iOS pass at that head. The downloaded signed Mac runtime
and integrated IPA verify managed/console acceptance, 16 iOS 15 images,
15 ELF/layout pairs, five runtime resources and 175 notices with empty
entitlements. No physical execution is claimed.

At `0f1ae04`, all 20 host jobs and iOS pass. The native 19-case readiness
reference, 99 input hashes, signed runtime and integrated IPA verify independently.
Device/VFS snapshots now preserve unread work and
share bounded thread admission with ioctl. At `fb72b68`, the expanded 31-case
fixture passes Linux/ARTBox comparison, including process-level death readiness
before ENTER_LOOPER. Transaction/object/one-way/death fixtures also pass through
readiness checks. Artifact provenance and 99 input hashes verify. All 20 host
jobs and iOS pass; the signed Mac runtime and integrated IPA independently verify
managed/console controls, 16 iOS 15 images, 15 ELF/layout pairs, five resources
and 175 notice hashes with empty entitlements.

At `8c67a93`, persistent native epoll interests pass the same 31 readiness cases,
thread teardown and MOD, ADD/DEL controls, last-close removal and actual
blocked-wait death notification. Artifact provenance and 100 input hashes verify.
All 20 host jobs and iOS pass; the downloaded signed runtime, Binder archives
and integrated IPA verify independently. The IPA has 16 iOS 15 images, 15
ELF/layout pairs, five resources and 175 notices with empty entitlements.
`epoll_driver_compared` remains false.

At `0cb0def`, the native control verifies that a Binder receive mapping retains
the original epoll interest after descriptor close. Reusing that descriptor
number creates a distinct interest; partial unmap retains the original, and
final unmap removes it. The downloaded kernel artifact and 100 input hashes
verify. Its full regression was superseded by `cf15426`, where all 20 host jobs
and iOS pass, including the same lifetime control.

A private native wake provider now uses Darwin kqueue, Linux eventfd and Windows
events. Its contract passes locally: 256 cross-thread handshakes, 16,000 concurrent
signals, retained/coalesced wakeups, object isolation and timeouts. All 41 Windows
host contracts pass; three native-backing cases remain skipped there. At
`cf15426`, downloaded Mac/Linux test logs independently verify kqueue/eventfd
execution and native EINTR. A requested 30 ms timeout takes 34 ms on Mac and
30 ms on Linux in those contract runs; these are not throughput measurements.
The kernel reference, signed ART runtime, Binder archives and integrated IPA
also verify independently at that head, including empty iOS entitlements.

Binder now exposes bounded wake subscriptions and keeps mapped, closed tokens
observable until final unmap. Local tests cover 64-listener exhaustion, independent
wake objects, unsubscribe during ongoing ioctls, stale subscription identities
and passive unmap detection outside VM locks. All 41 Windows host contracts and
strict Android ARM64 compilation pass. At `a3129ca`, the downloaded native Linux
comparison verifies these controls and 103 input hashes. Two diagnostic uploads
initially failed with ENOTFOUND; rerunning the failed Mac job and dependents
restored all 20 host jobs to green without changing checks. The iOS build passes.
The downloaded signed ART runtime, Binder archives and integrated IPA also
verify at that head, including all 16 iOS 15 images and empty entitlements.

Guest `epoll_create1`, `epoll_ctl` and `epoll_pwait` now implement bounded,
level-triggered Binder interests through the six-argument VFS entry. The shared
31-case readiness fixture passes locally through actual guest syscall dispatch,
as do mapped fd reuse, fair scanning, full/partial output faults, independent
waiters, close/reuse during a wait, injected interruption, deadlines during
repeated hints and resource/owner controls. All 41 available Windows contracts
pass; three native-backing cases remain skipped. Strict Android ARM64 compilation
passes. At `08aaf4e`, all 20 host jobs and the iOS build pass; downloaded
native comparison verifies all 104 inputs and the production epoll comparison.
Signed Mac ART, Binder archives and the integrated IPA verify independently:
16 iOS 15 images, 15 ELF/layout pairs, five runtime resources and 175 notices,
with empty entitlements. Guest eventfd/timerfd, real servicemanager execution
and signed Binder attachment remain required for M4.

An original shared eventfd fixture now covers counter/semaphore transfers,
readiness, saturation, sizes, copy faults and seek. Native controls add nine
observed blocking read/write/epoll cases for wakeup, real signal interruption
and descriptor close/reuse, plus persistent epoll interest lifetime. Both
units compile strictly for Android ARM64. At `ca55328`, Linux x86_64 and ARM64
both pass all 63 shared cases and nine native blocking cases; downloaded logs,
artifact digests and binary architectures verify. Guest eventfd2/read/write
now pass the same fixture locally, plus epoll wakeup, weak-interest lifetime,
injected interruption after close/reuse, semaphore contention, wait admission,
VM/copy-fault controls and 2,048 concurrent increments. At `bea672d`, downloaded
Linux x86_64 and ARM64 evidence verifies the 63 shared native/guest cases;
Darwin ARM64 verifies the guest counter and real-provider wake controls.
Binder regression and all 104 input hashes verify as well.
All 20 host jobs and the iOS build pass at that head. The downloaded signed
Mac ART runtime, Binder archives and integrated IPA verify independently;
the IPA retains 16 iOS 15 images, 175 notices and empty entitlements.

An original timerfd reference now covers 59 shared assertions for monotonic
expiration counts, relative/absolute deadlines, disarming, periodic gettime,
copy-fault ordering and malformed inputs. Six native syscall-observed read/epoll
cases add timer wakeup, signal interruption and descriptor close/reuse. Both
units compile strictly for Android ARM64. At `fb53373`, both Linux architectures
pass and downloaded artifacts verify. Copyout transferred four bytes before
fault on x86_64 and one on ARM64, consuming the expiration in both cases.
Portable monotonic timerfd now passes the shared fixture locally, plus real
provider wakeups, controlled deadline/missed-period tests, retained reads after
close/reuse, injected interruption, admission and page-bounded copyout. It uses
the existing 5 ms wait checks, not a native timer per descriptor. At `eacb3dc`,
all 20 host jobs and iOS CI pass. Downloaded Linux x86_64/ARM64 results verify
the paired 59-case timer and 63-case eventfd fixtures; the Mac ARM64 guest
contracts, Binder comparison, signed ART runtime, 49-object Binder
build and integrated IPA also verify independently. The IPA has 16 iOS 15
images, 15 ELF/layout pairs, 175 notices and empty entitlements. Its signed Mac
runtime creates the VM in 62.840 ms, with 67,649,536 bytes peak RSS; these are
correctness-run observations, not phone performance measurements.

The next change selects unchanged AOSP `Looper.cpp` and `Timers.cpp` and an
original 43-assertion shared caller for wake, TLS ownership, callbacks,
descriptor replacement, message ordering/cancellation and timer delivery.
At `ce43021`, both native Linux architectures pass all 43 assertions and the
two controls for retained callbacks and omitted wakeups. Downloaded binaries,
17 objects, 187 upstream files and 22 project inputs verify independently on
each architecture. The local Android caller and ten support units also pass
instruction audits; the broader Binder build has 51 objects in three archives.

A five-image signed Looper profile now uses the four ART bootstrap dependencies
plus the real Looper caller. Bionic includes its original public eventfd wrapper;
339 units compile locally with no forbidden native instructions. The Apple
harness checks exact shared results and cleanup of one Bionic worker in each
fresh process, including both negative controls. At `480238f`, all 22 host jobs
and iOS CI pass. Downloaded signed Mac results independently verify all 43
assertions and both deliberate failures, with one worker reaped in every case.
The normal fixture takes 14.976 ms; image loading/relocation takes 28.089 ms.
Both macOS and iOS 15 Looper frameworks, their layouts, 20 notice hashes and
107 project inputs verify. These are correctness-run measurements on Mac;
the new framework is not yet part of the app IPA. Binder attachment and real
servicemanager registration/ping/death acceptance remain ahead. M4 is open.

The next shared Binder fixture exercises `GET_NODE_INFO_FOR_REF`, which real
servicemanager uses for client tracking. It checks manager-endpoint permission,
reserved-field validation, bad input and read-only output, pending owner
acknowledgements, duplicate acquires, retained local objects, a second importing
endpoint, owner death and weak-only
handles. Synchronous message barriers separate each expected count. At `fb0232b`,
the real Linux driver passes all ten phases; downloaded provenance and 105 input
hashes verify. The portable ioctl now derives counts from existing endpoint
references, owner acknowledgements and local buffer claims under the Binder
mutex. Its VM/error/admission tests and all 43 available local host contracts
pass; three native-backing contracts remain Windows skips. The same lifecycle
fixture passes the actual Linux and portable-driver comparison at `8b8c42c`;
downloaded artifact provenance and 105 input hashes verify. All 22 host jobs and
the iOS build pass. The integrated IPA independently verifies 16 iOS 15 images,
15 ELF/layout pairs, five runtime resources and 175 notices with empty
entitlements. Its signed Mac run creates the VM in 72.326 ms and completes the
managed/console controls; physical execution remains unverified.

The Binder archive build now reserves x27/x28 in addition to x18 and checks each
object's executable byte count and instruction boundary. This exposed an actual
host-thread-pointer read in BufferedTextOutput's compiler TLS. The existing
build-time absolute-TLSDESC adapter is now selected for that pinned source, its
per-thread object and initialization guard. Source, symbol and access-count
checks retain the original object for comparison. Linking and signed execution
of libbinder, including its thread-exit destructors, remain ahead.

At `25c3c1a`, the Binder CI build passes. Downloaded evidence independently
verifies all 51 objects, three archives, 333 upstream files, 19 project inputs
and 96,434 instructions with no forbidden instructions. Both original and
adapted TLS assembly reproduce the CI object bytes when reassembled locally.
The next source selection adds nine original AOSP thread, clock, property,
native-handle, multiuser, ashmem, trace and vendor-loader units. All 60 local
objects in six archives pass the audit: 100,295 instructions, with none forbidden.
The build takes 115.737 seconds locally; 19 package/TLS controls pass. A private focused
Binder link then reaches four unresolved imports: timespec_get, fnmatch and two
Android loader APIs. Original servicemanager main and ServiceManager translation
units compile in a private dependency probe; full VINTF linkage, access policy,
signed service registration and ping/death acceptance are still required.

At `c46c855`, all 22 host jobs and iOS CI pass for the expanded Binder archives.
Downloaded evidence verifies 60 objects, six archives, 349 upstream files and
100,295 instructions, including byte-identical TLS reassembly. The integrated
IPA verifies 16 iOS 15 images, 15 ELF/layout pairs, five resources and 175 notices
with empty entitlements; its signed Mac ART/console controls pass.
The next shared libc contract covers 50 pattern/time expectations and two
deliberate failure controls. Its caller first failed to link against the prior
Bionic image with the expected missing fnmatch/timespec_get imports. The original
upstream units are now selected, with their BSD/ISC notices. The native Linux
oracle and both signed Bionic profiles must pass before artifact staging; these
new execution results pass at `6c44531`: all 22 host jobs and iOS CI are green.
Downloaded evidence verifies the shared caller, original source/object hashes,
Linux executable and all three profiles' 50 cases and both controls. The native
Linux process takes 0.698 ms in this correctness run. All 341 selected Bionic units compile locally
with 117,840 audited instructions and none forbidden; the caller now links into
the Bionic startup image and its native Linux reference.
The same head's 60 Binder objects, all 100,295 instructions and integrated ART
IPA verify independently. The IPA retains 16 iOS 15 images, 15 ELF/layout pairs,
five runtime resources, 175 notices and empty entitlements. Its signed Mac VM
starts in 60.892 ms; load/relocation takes 31.056 ms and peak RSS is 66,797,568
bytes. These are correctness-run observations, not iPhone measurements.

The next loader change adds an explicitly configured visible namespace for each
immutable signed group, context-specific tokens and the 48-byte Android extension
bridge. Portable tests pass for open/reference behavior, invalid and foreign
tokens, unreadable guest records, pending-error preservation and concurrent calls.
Original AOSP libdl/libdl_android and the 51-case NDK fixture now compile and
link locally with no forbidden instructions. At `a38f40f`, all 22 host jobs and
iOS CI pass. Downloaded evidence verifies the 51 signed extension cases, both
precise controls, 32 ordinary loader cases, six thread-error cases and the four
ELF/eight Mac-and-iOS framework layouts. The fixture process takes 21.040 ms.
The integrated ART IPA still uses its existing 15-image group; libdl_android is only added to
the dedicated loader fixture. All 43 available Windows contracts pass, with the
three existing native-backing skips; strict NDK compilation passes too. A private
focused ProcessState/IPCThreadState/BBinder/Parcel caller now links against the
60 original Binder/platform objects, verified `6c44531` runtime images and the
new local loader frontends with no missing symbols. This establishes link
closure only. Real VINTF, servicemanager access policy and signed Binder/service
execution remain ahead.

The same head's integrated ART IPA independently verifies 16 iOS 15 images,
15 ELF/layout pairs, five resources, 175 notices and empty entitlements. Its
signed Mac VM starts in 96.228 ms with 67,567,616 bytes peak RSS; managed,
console, signal-chain and worker-cleanup controls pass. These measurements are
from correctness runs, not an iPhone.

The original 39-unit AOSP XSdc compiler now reproduces four APEX parser files
locally, including deterministic generation and both malformed-input controls.
Six evidence/output-path tests pass. The new portable VINTF build selects 27
original VINTF/kernel-config units, three libkver units, TinyXML2 and two generated
units. All 33 objects in four archives pass locally, with 130,989 audited
instructions and none forbidden; compilation takes 86.086 seconds. The private
full-servicemanager link is
down to eight unresolved symbols: Access construction/context formatting, three
libc++ regex helpers, regcomp/regfree and security_policyvers. At `cce2bb9`, all
22 host jobs and iOS CI pass. All three XSdc artifacts independently reproduce
the four canonical outputs and both rejection controls. The downloaded VINTF
artifact verifies 33 objects, four archives and 130,989 instructions, with a
15.460-second Mac compilation. The integrated IPA retains 16 iOS 15 images,
15 ELF/layout pairs, five resources, 175 notices and empty entitlements. Its
signed Mac VM starts in 122.485 ms with 67,616,768 bytes peak RSS; managed,
console, signal-chain and worker-cleanup controls pass. These are correctness
measurements, not physical-device results.

The `580bacb` change selects the four original Bionic regex implementations using
their original NetBSD build flags. The new Android LP64 fixture compiled and
failed its first link against the preceding runtime on all four missing regex
functions. Its 120 expectations and two exact failure controls are now wired
into the native Linux reference and both signed Bionic profiles, alongside the
existing 50 libc expectations. All 22 host jobs and iOS CI pass. Downloaded Linux
and signed Mac artifacts independently verify both caller hashes, 14 upstream
inputs, all 170 expectations and four exact negative controls. The integrated
ART IPA verifies 16 iOS 15 images, 15 ELF/layout pairs, five resources, 175
notices and empty entitlements. Its signed Mac VM starts in 100.356 ms with
67,551,232 bytes peak RSS; these are correctness-run measurements. The separate private full-service probe, including
one original libc++ regex object, is down to three policy-related imports. It is
not service execution. Real access policy, signed service registration and
cross-thread Binder ping/death acceptance remain ahead.

At `4d3a124`, 18 of 19 host jobs pass. Integrated ART IPA staging rejects
an interleaved startup log line in the missing-class control. Bounded single-write
acceptance records fix the framing without weakening the exact-line checks.
At `5e821ca`, all 19 host jobs and the iOS build pass; the downloaded runtime
verifies 96 source hashes and all managed, console and missing-class controls.
The integrated IPA independently verifies 16 iOS 15 images, 15 ELF/layout pairs,
five runtime resources and 175 notice hashes, with empty entitlements.

M3 main artifacts were downloaded and verified independently: 16 signed iOS 15
images, 15 ELF/layout pairs, five runtime resources and 175 notice hashes.
Launcher UI remains deferred. Historical debugging results are retained in
[the bring-up history](m3-bringup-history.md).

## Measurements and risks

One signed Mac main-branch run at `8cf6315` creates the VM in **54.157 ms**,
loads/relocates in 32.226 ms and reaches **66,699,264 bytes peak process RSS**.
The complete bootstrap/runtime phase is 57.948 ms and includes VM creation. Managed bytes
before shutdown are 636,072. These are traced correctness observations, not
interpreter throughput or iPhone memory measurements.

The next three risks are transaction-buffer ownership and bounded memory;
reference/death-notification races; and real servicemanager's polling and
dependency requirements. Physical ART execution remains unverified despite the
M0/M1 hardware success; checks remain waived for every milestone. See
[the M1 device record](acceptance/m1.md#subsequent-physical-device-observation).
