# ARTBox status

M0-M3 are merged and tagged. **Real AOSP ART executes hello DEX on macOS
ARM64 through signed frameworks**, with collection, exceptions, native-thread
attachment and shutdown verified. M3's shared console and integrated ART IPA
also pass automated acceptance. Target: ordinary signed arm64 **iOS 15+**, no JIT or private
entitlements. Physical checks are waived; iPhone execution is unverified.

## What runs

- M0: portable startup and the independent iOS console print `ARTBox ready`.
- M1: a static NDK ARM64 hello runs on Mac through both signed packaging
  prototypes, with five translated syscalls and exit zero.
- M2: pinned AOSP Bionic, Scudo and GWP-ASan pass **328/328** expectations in
  both normal and forced-sampling processes. Six Bionic pthreads exercise
  allocation, rooted files, mappings, synchronization, TLS, errno and cleanup.
  The integrated diagnostic IPA includes the same four signed libraries.
- M3 Linux references: both original and high-heap AOSP profiles execute hello,
  explicit GC, exceptions, attachment cycles and VM shutdown with JIT disabled.
- M3 signed Mac runtime at `68400fe`: 15 native images, 38 constructors, actual
  JavaVM creation, exact hello output, a managed graph surviving collection,
  three exceptions, four attachment cycles, thread-state isolation, shutdown
  and eight reaped threads. A missing-hello process fails class lookup as required.
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

The verified ART IPA contains 16 signed iOS 15 Mach-O images, 15 original ELF
resources, boot/managed/hello DEX and ICU data. Device execution remains
unverified. Binder/services, Activities, AndroidX APKs, graphics and audio remain future
milestones. The runtime is a one-shot diagnostic; repeated VM creation and
general dynamic library growth/unloading are not supported. AOT/OAT and host
dex2oat remain future coverage under interpreter-only M3 acceptance.

Additional syscall families, broader process and signal behavior, mutable cwd,
directory mutation and broader proc metadata remain unsupported. The Windows
native file provider is absent; portable VFS tests inject a provider there.
Unsupported operations retain explicit errors. See the individual contracts.

## Current work

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
VM/copy-fault controls and 2,048 concurrent increments. Native production
comparison and signed-runtime regression await CI. Timerfd and actual AOSP
Looper/servicemanager execution remain ahead.

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
dependency requirements. Physical execution remains unverified; checks are
waived for every milestone.
