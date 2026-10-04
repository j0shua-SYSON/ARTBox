# ARTBox status

M0-M2 are merged and tagged. **Real AOSP ART now executes hello DEX on macOS
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

At `c7807da`, all 18 host jobs and the iOS build pass, including console controls
and the integrated ART IPA. The 34 Windows contracts,
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

M3's implementation and downloaded artifact verification pass at `c7807da`.
Complete final review, CI, merge and tag before starting M4's userspace Binder
and real AOSP servicemanager. The console includes scoped autorelease handling
for guest pthread callbacks and cleans partial resource copies on setup failure.
Launcher UI remains deferred. Historical debugging results are retained in
[the bring-up history](m3-bringup-history.md).

## Measurements and risks

One signed Mac correctness run at `c7807da` creates the VM in **113.287 ms**,
loads/relocates in 85.288 ms and reaches **67,420,160 bytes peak process RSS**.
The complete bootstrap/runtime phase is 121.504 ms and includes VM creation. Managed bytes
before shutdown are 636,072. These are traced correctness observations, not
interpreter throughput or iPhone memory measurements.

The next three risks are iOS virtual/resident memory limits under an ordinary
profile; extending tested synchronization and signal subsets to Binder/services;
and matching Binder ownership, wire semantics and death notifications without
a guest kernel. Physical execution remains a separate unverified evidence level.
