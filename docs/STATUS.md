# ARTBox status

M0-M2 are merged and tagged. **Real AOSP ART now executes hello DEX on macOS
ARM64 through signed frameworks**, with collection, exceptions, native-thread
attachment and shutdown verified. M3 remains open while that runtime is embedded
in the iOS app. Target: ordinary signed arm64 **iOS 15+**, no JIT or private
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

At `68400fe`, all 17 host jobs and the iOS build pass. The 34 Windows contracts,
36 native Linux ARM64 contracts, downloaded source/object hashes, signed
framework layouts and diagnostic IPA verify independently. See
[signed ART evidence](m3-signed-runtime.md), [M2 acceptance](acceptance/m2.md),
[syscall subsets](syscalls.md) and [architecture decisions](DECISIONS.md).

## What does not run

The current verified iOS IPA still contains M1/M2 diagnostics. The new ART app
integration and guest-stdout callback require native CI and IPA verification.
Binder/services, Activities, AndroidX APKs, graphics and audio remain future
milestones. The runtime is a one-shot diagnostic; repeated VM creation and
general dynamic library growth/unloading are not supported. AOT/OAT and host
dex2oat remain future coverage under interpreter-only M3 acceptance.

Additional syscall families, broader process and signal behavior, mutable cwd,
directory mutation and broader proc metadata remain unsupported. The Windows
native file provider is absent; portable VFS tests inject a provider there.
Unsupported operations retain explicit errors. See the individual contracts.

## Current work

Stage the same 15 signed libraries, original ELF resources, boot/managed/hello
DEX and ICU data in a separate ART iOS build. Require exact producer revisions,
complete managed acceptance, a tested guest-stdout sink and the missing-class
control before packaging. The app copies data into its container and calls the
same native runtime on a background queue. Launcher UI remains deferred.

The staging validation tests and all 34 local CTests pass. The new signed
console check and integrated ART IPA remain pending. M3 is unmerged and untagged;
M4 starts only after M3's remaining automated acceptance passes. Historical
debugging results are retained in [the bring-up history](m3-bringup-history.md).

## Measurements and risks

One signed Mac correctness run creates the VM in **73.569 ms**, loads/relocates
in 71.803 ms and reaches **66,977,792 bytes peak process RSS**. The complete
bootstrap/runtime phase is 79.224 ms and includes VM creation. Managed bytes
before shutdown are 636,072. These are traced correctness observations, not
interpreter throughput or iPhone memory measurements.

The next three risks are iOS virtual/resident memory limits under an ordinary
profile; extending tested synchronization and signal subsets to Binder/services;
and matching Binder ownership, wire semantics and death notifications without
a guest kernel. Physical execution remains a separate unverified evidence level.
