# ARTBox status

M0 and M1 are merged and tagged. The iOS 15+ target has the ARTBox console;
the identical static NDK ARM64 hello runs through both signed packaging routes
on macOS and unchanged on Linux ARM64. Each Mac route passes 100 invocations
and five syscall counts. See [M0](acceptance/m0.md), [M1](acceptance/m1.md) and the
[loader decision](loader-design.md). The Apple-linker wrapper is selected for M2.

Physical checks are waived as milestone gates. Physical iPhone execution remains
unverified. Launcher development is deferred; runtime milestones take priority.

## M2: real Bionic startup and threads run; acceptance is incomplete

At `e828dad`, [host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34816736646)
and the [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34816736632)
pass. Downloaded ELF, signed macOS/iOS framework, notice and IPA hashes/layouts
match their reports. The newer Bionic frameworks are separate artifacts; the
regression IPA still embeds the M1 demo, not the M2 suite.

- The pinned 221-source selection builds real AOSP Bionic with Scudo, GWP-ASan,
  baseline Arm string routines, property parsing and two pinned compiler-rt
  members. Original and adapted objects retain provenance and complete notices.
- The signed two-image load group initializes real Bionic main-thread TLS,
  relocates data, runs three constructors and executes a dynamically linked NDK
  client: 146 allocator/device checks and 19 futex checks pass in both normal and
  forced GWP-ASan processes. The latter observes 30 guarded main-thread allocations.
- Four joinable and two detached Bionic workers complete 32 allocation iterations
  each, mutex/condition synchronization, distinct errno storage, key destructors,
  return values and explicit pthread_exit. A native reaper joins before clear-TID
  wake or detached-stack release. See [startup and thread evidence](bionic-startup.md).
- All 16 portable CTest contracts pass on Windows, macOS and Linux. Thread tests
  include failed creation, 130 reaps, delayed native completion and actual POSIX
  supplied stacks. Futex tests include timeouts, bitsets and 512 enqueue/wake races.
- Existing paired NDK/Linux oracles remain green: 35 memory, 36 startup-service,
  19 futex, 35,908 string and 123 binary128 cases, plus syscall/TLS boundary tests.
  ELF metadata and data relocation tests compare against LLVM.

One correctness run measures 10.571 ms for native thread setup, Bionic startup,
all clients and reaping; forced GWP sampling measures 13.748 ms. These include a
futex timeout, scheduling and trace output. Scudo reserves about 8.25 GiB of
virtual address space; resident memory and iOS headroom are not yet measured.

At `cc6b074`, signal-mask inheritance/isolation and the expanded 53-case startup
syscall caller pass signed macOS and both Linux profiles; the iOS build is green.
This does not implement signal delivery or modify host signal masks. The next
measurement records process peak RSS and thread-client elapsed time, and confirms
that forced GWP sampling reaches each of the six workers.

## Next work and remaining acceptance

Add rooted regular-file operations and
file-backed mappings. General dependency namespaces, symbol-version rules,
cycles, constructor lifecycle and guest ELF TLS templates remain incomplete;
the current executed load group is deliberately fixed. The [M2 contract](m2-contract.md)
requires a published multithreaded NDK denominator with at least 90% passing,
mandatory thread/file/memory success, measured resident memory, and an iOS build
containing that same suite. M2 is not tagged or complete. M3-M7 remain unimplemented.

## Three largest M2 risks

1. Completing dynamic dependency, version and ELF TLS behavior while preserving
   signed executable bytes and Android/Apple ABI boundaries.
2. Matching Linux regular-file/mapping and broader thread semantics well enough
   for the mandatory NDK suite; signal delivery and several syscall families remain open.
3. Fitting real Bionic/Scudo and future ART within ordinary iOS memory limits,
   then packaging the integrated suite into the device build.
