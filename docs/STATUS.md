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
three native-backing contracts remain unavailable there. The shared blocking
ping/reply and Linux/ARTBox wait comparison await CI. Readiness polling, actual
service execution and signed Binder attachment remain ahead.

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
