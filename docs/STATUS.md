# ARTBox status

M0 and M1 are merged and tagged. The iOS 15+ target has the ARTBox console;
the static NDK ARM64 hello runs through both signed packaging routes on macOS
and unchanged on Linux ARM64. See [M0](acceptance/m0.md), [M1](acceptance/m1.md)
and the [loader decision](loader-design.md). The wrapper is selected for M2.

Physical checks are waived as milestone gates. Physical iPhone execution remains
unverified. Launcher development is deferred; runtime milestones take priority.

## M2: real Bionic threads and rooted files run; acceptance is incomplete

At `ce6c6eb`, [host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34820500656)
and the [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34820500663)
pass. Downloaded inputs, signed framework layouts, notices and IPA match their
reports. Bionic frameworks are separate artifacts; the regression IPA still
embeds M1, so the integrated M2 device build remains outstanding.

- A pinned 221-source selection builds real AOSP Bionic with Scudo, GWP-ASan,
  Arm string routines, property parsing and two pinned compiler-rt members.
  Source provenance, adaptation inventories and complete notices are retained.
- Signed ARM64 macOS code initializes real Bionic TLS, relocates data, runs
  three constructors and passes 146 allocator/device checks and 19 futex checks.
- Four joinable and two detached Bionic workers each perform 32 allocation
  iterations, mutex/condition synchronization, errno isolation, signal-mask
  inheritance, key destructors, and regular-file write/stat/seek/read round trips.
  A native reaper waits for actual worker termination before releasing stacks.
- The same 41-case NDK file caller passes signed macOS and both native Linux
  ARM64 profiles. The oracle caught and corrected an ARM64 open-flag mismatch.
  Rooted paths reject traversal and symlinks; directory-relative handles remain
  pinned. See [files](files.md) and [startup evidence](bionic-startup.md).
- All 17 portable contracts pass on Windows, macOS and Linux. Paired NDK/Linux
  oracles also cover 35 memory, 53 startup-service, 19 futex, 35,908 string and
  123 binary128 cases, plus syscall and TLS boundaries.

One correctness run takes 12.631 ms for startup and all clients, including
3.357 ms for the pthread/file workload and 0.457 ms for the raw file caller.
Forced GWP sampling takes 18.454 ms overall and observes all 192 worker mallocs
in guarded allocations. Entire-process peak RSS is 6,078,464 bytes normally and
5,931,008 bytes sampled; Scudo reserves about 8.25 GiB of virtual address space.
These macOS measurements include tracing and a timed wait, and are not iPhone
memory or throughput measurements.

The current extension adds native non-executable file mappings and a separate
43-case NDK caller for shared/private visibility, descriptor lifetime, permission
ceilings, sync, discard and partial unmap. All 18 local contracts and the NDK
build/instruction checks pass; native macOS/Linux mapping execution awaits CI.

## Next work and remaining acceptance

Verify file-backed mappings, then complete general dependency namespaces,
symbol-version rules, cycles, constructor lifecycle and guest ELF TLS templates.
The executed load group is currently fixed. The [M2 contract](m2-contract.md)
requires a published NDK denominator with at least 90% passing, mandatory
thread/file/memory success, and an iOS build containing that same suite and
libraries. M2 is not tagged or complete. M3-M7 remain unimplemented.

## Three largest M2 risks

1. Completing dependency, version and ELF TLS behavior while preserving signed
   executable bytes and Android/Apple ABI boundaries.
2. Matching Linux mapping and broader thread semantics; signal delivery and
   several syscall families remain unsupported.
3. Fitting Bionic/Scudo and future ART within ordinary iOS memory limits, then
   packaging the integrated suite into the device build.
