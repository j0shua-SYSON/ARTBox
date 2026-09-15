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
  versions, relocates data atomically, and runs three constructors.
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

See [M2 acceptance](acceptance/m2.md) for exact revisions, CI links, artifact
hashes, counts and limitations. One traced Mac process takes 17.503 ms for
startup and clients, including 2.898 ms for the threaded workload. Forced GWP
sampling takes 19.755 ms and observes all 192 worker mallocs. Peak process RSS
is 6,717,440 / 6,488,064 bytes. These are correctness observations including
timed waits, not steady-state performance or iPhone measurements.

## What does not run

ART/DEX execution, Binder/services, Android Activities and APKs are not implemented.
The M2 runner is diagnostic and single-use; unexpected contract failures can
terminate its process. General dlopen scope growth, unloading/finalization,
other ELF TLS models, signal delivery, broader proc files, mutable directories
and additional syscall families remain unsupported. The Windows native file
provider is not implemented; portable VFS tests use an injected provider there.

## In progress: M3 ART bring-up

The [M3 contract](m3-contract.md) specifies real ART/DEX execution, runtime
code-generation checks and shared iOS integration. Its first host probe tests
two low-address heap reservations and compares the default Apple null guard
with a reduced guard in native executables. ARM64 macOS rejected the reduced
guard before entry (EBADMACHO), consistent with XNU's hard 4 GiB requirement.
A checked heap-relative reference codec and retained negative launch test now
cover the selected alternative. At `0f1aed8`, signed ARM64 macOS reports the
exact EBADMACHO errno (88), and native memory above 4 GiB round-trips through
the codec. Both iOS 15 probe binaries link and pass signature/layout checks;
their launch behavior is unverified. All required CI is green at that revision.

The next fixture selects AOSP's real `ObjectReference`, `HeapReference` and
`CompressedReference` headers and tests 19 cases with heap poisoning both off
and on. A narrow source overlay redirects raw-pointer compression through the
checked codec while retaining AOSP's four-byte storage and poison encoding.
The native Linux control uses the original headers and absolute low addresses;
the signed Mac wrapper uses heap-relative high addresses. At `2586a50`, all
38 cases pass on each native OS, with independently checked stored bit patterns
and retained acquire/release instructions. Four Mac/iOS frameworks pass layout,
signature and notice checks. See [reference evidence](m3-references.md).
This does not yet boot ART, collect objects or execute DEX.

At `28b13ff`, AOSP's DEX loader accepts the original hello fixture and rejects
five malformed variants on both native Mac and Linux ARM64. The same selected
sources link into an ordinarily signed iOS 15 framework. The 448-byte DEX,
component/source pins, compiled objects, notices and signed binaries are checked
against downloaded evidence. Windows compiles 31 Android source units. See
[DEX loading evidence](m3-dex.md); inspecting instructions is not execution.

The portable [class-library builder](m3-classlib.md) now compiles 3,227 pinned
AOSP Java inputs into 6,422 classes and two DEX039 files containing 7,309 class
definitions. The output includes selected corresponding source and notices.
Local Java/D8 compilation passes; native AOSP class-library verification and
its iOS 15 library build pass at `5afd004`. All five class-library cases pass
on each native Mac/Linux ARM64 host. Downloaded source, notices, compiled
objects, DEX files and signed binaries match the recorded provenance; both
platforms produce identical DEX. Native libraries, resources and ART interpreter
startup remain incomplete.

The [runtime policy fixture](m3-runtime-policy.md) tests a forbidden JIT factory
and optional Rust stack-trace name formatting. Eight native name cases and the
factory's fatal error path pass locally; Mac/Linux CI and the signed iOS 15
framework pass at `c11f5a6`. Downloaded binaries, notices, source hashes and
signature pages match their evidence. These functions do not start ART or
establish that runtime startup avoids executable allocations.

The original interpreter/runtime source selection and its support dependencies
are pinned in `third_party/art/runtime-sources.json`. Run
`python -B scripts/art_runtime_sources.py` to prepare and verify those inputs
using the configured cache. The complete portable runtime builder, native Linux
reference startup and Apple ABI integration remain in progress. Static Bionic
and public-NDK shared-link limitations are recorded in ADR 0037; they are not
waived by the successful source compilation probes.

Pin and review only the ART sources and dependencies required to execute a
hello-world DEX with the AOSP interpreter. Establish a host reference and build
contract before adapting native entry, thread, memory and code-loading paths.
Keep all generated native code in build-time signed artifacts; document the
cost of the interpreter and each disabled runtime optimization. M4-M7 follow
after M3 acceptance is green.

The three largest M3 risks are ART's dependency/build footprint, its assumptions
about signals/thread suspension and executable mappings, and ordinary iOS memory
limits. The current Scudo reservation is about 8.25 GiB of virtual address space;
Mac RSS does not establish an iPhone memory budget. ART's managed ABI and stack
walking also need explicit validation across signed native boundaries.
