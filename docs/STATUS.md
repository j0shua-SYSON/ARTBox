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
commands and validates transaction/object snapshots. Local checks pass 36 host
contracts and 60 comparisons against actual NDK UAPI constants plus an
independently produced byte fixture. A bounded receive-arena primitive also
passes local ownership/exhaustion checks and 8,000 concurrent lifecycles;
its native mapping/provider is not connected. Driver delivery, polling, references,
death notifications and real servicemanager are next; M4 is not complete.

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
