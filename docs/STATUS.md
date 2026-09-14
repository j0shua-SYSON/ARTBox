# ARTBox status

M0 and M1 are merged and tagged. The project targets ordinary signed arm64
**iOS 15+** apps. Physical checks are waived as milestone gates; physical iPhone
execution remains unverified. Runtime milestones take priority over launcher UI.

## M2: dynamic Bionic, threads, files, mappings and ELF TLS run

At `32121e5`, [host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34825715581)
and the [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34825715580)
pass. Downloaded original ELF inputs, eight signed Mac/iOS framework layouts,
notices, TLS assembly/objects and the Linux TLS report match their manifests.

- Real pinned AOSP Bionic starts with Scudo and GWP-ASan. The selected build
  contains 221 sources plus two pinned compiler-rt members; notices are complete.
- The portable manifest loader relocates four signed libraries, resolves
  dependencies and symbol versions, handles cycles, and runs three constructors.
  Hidden/default version calls return 46 in signed Bionic and native Linux.
- Both normal and forced-sampling processes pass 146 allocator/device checks,
  19 futex checks, 41 regular-file checks and 43 file-mapping checks.
- Four joinable and two detached Bionic workers each run 32 allocation/file
  iterations with mutex/condition synchronization, errno and key isolation,
  signal-mask inheritance, return/exit handling and destructor cleanup. The
  native reaper waits for actual thread termination before releasing stacks.
- Two standard ELF TLS templates provide initialized, zero-filled and aligned
  storage for main plus six workers. A precompiled TLSDESC bridge uses Bionic's
  DTV/accessor. Original NDK ELF code passes the paired Linux test; x2-x17 and
  all full SIMD registers survive the initial and subsequent resolver calls.
- All 19 portable contracts pass on Windows, macOS and Linux. Additional paired
  NDK/Linux oracles cover memory/startup syscalls, strings and binary128 math.

One traced Mac process takes 14.566 ms for startup and all clients, including
3.768 ms for the threaded workload and 0.796 ms for files/mappings. Forced GWP
sampling takes 22.664 ms overall and observes all 192 worker mallocs. Peak process
RSS is 6,635,520 / 6,324,224 bytes. These are correctness-run observations, not
steady-state performance or iPhone measurements. ELF TLS access currently saves
full SIMD state around Bionic's accessor; its isolated cost is not yet measured.

## Remaining M2 acceptance

The iOS regression IPA still runs M1. Integration now moves the exact M2 runner
into the shared Apple platform layer and embeds its four tested libraries and
ELF resources in an iOS 15 app. The new CI job waits for host and Linux success,
checks the source revision and every signed payload, then builds the M2 IPA.
That integrated build passes at `545f8b6` in [CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34826492425). The downloaded IPA is verified: all seven Mach-O images, four ELF resources,
notices, the manifest and merge provenance agree. Its SHA-256 is
`b1ef0c659037cc1de71b14bdb0c21096d52dda373ec9855eab05fe697ac59f90`
(795,303 bytes). Physical execution remains unverified.

The integration denominator is frozen at 328 expectations. The full suite now
adds the existing 35-case anonymous-memory caller and 18 pthread timeout cases;
CI is pending. The 22-case proc contract is written before its implementation. The suite must pass at least 90%, including mandatory
thread/file/memory behavior. General dlopen scope growth, preinit/finalization,
signal delivery and additional syscall families remain unsupported. M2 is not
tagged or complete; M3-M7 remain unimplemented.

## Three largest M2 risks

1. Finishing the integrated iOS target while preserving the tested guest ABI
   and signed executable layout.
2. Closing the remaining NDK acceptance cases without hiding unsupported Linux
   behavior behind the passing allocator/file workload.
3. Bionic/Scudo and future ART memory use on ordinary iOS devices; host process
   RSS and virtual reservations do not establish an iPhone memory budget.
