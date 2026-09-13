# ARTBox status

M0 is merged and tagged `m0`: a portable startup contract and an iOS 15+ UIKit
log console. Host tests verify exactly `ARTBox ready` and exit code 0. Python
automation passes Windows, macOS, Linux and iOS CI; see [M0 acceptance](acceptance/m0.md).

M1 meets its automated acceptance: the same static, libc-free NDK ARM64 ELF
runs natively on macOS through both signed Mach-O conversion and linker-wrapper
frameworks. Each passes 100 hello/exit invocations and five syscall counts.
Native ARM64 Linux runs the original ELF unchanged. The iOS 15 device build
embeds both signed frameworks and verifies the IPA; its console uses the same
entry tested on the Mac. See [M1 evidence](acceptance/m1.md) and
[PR #2](https://github.com/j0shua-SYSON/ARTBox/pull/2).

Physical-device checks are waived for all milestones by the project owner.
Physical iPhone execution and lifecycle behavior remain unverified. Automated
acceptance, CI and artifact checks remain required.

Launcher development is deferred while the runtime milestones take priority.
The iOS target retains the verified M1 console and both signed demo frameworks.

The Apple-linker wrapper is selected for M2 based on the measured M1 experiment;
the direct converter remains a comparison prototype. The [loader design](loader-design.md)
was recorded before implementation. Complete dynamic Bionic, ART, Binder, graphics and APK execution remain future work.

## M2 in progress: dynamic Bionic

M2 must load real Bionic dynamically and run an NDK suite with threads, files
and mmap. The portable reader now validates ET_EXEC/ET_DYN program headers,
TLS templates, RELRO ranges and file-backed virtual views. Four views of real
NDK shared objects, including stripped section tables, match LLVM; ten malformed
variants are rejected. Dynamic-table parsing and GNU/SysV symbol lookup now
have portable tests and LLVM comparisons on real NDK files. Data relocation
now handles AArch64 RELA/RELR, GOT/PLT and ordinary symbol references, with
136 writes per real NDK fixture matching LLVM byte for byte. A failed operation
leaves destination data unchanged. Symbol versions, the dependency namespace,
runtime TLS, IFUNC and constructors remain unimplemented;
see [dynamic loader details](dynamic-loader.md). A new controlled dynamic wrapper
packs a Bionic syscall slice with separate signed RX/RW sections; Apple layout
verification and 100 native macOS iterations pass. It also builds/signs for
iOS 15 as a separate framework; see [dynamic-wrapper evidence](dynamic-wrapper.md).
This slice contains actual Bionic syscall code and its errno setter, with test
errno storage. It is not complete libc startup. The M1 packer remains unchanged.

The Bionic Android 15 source archive and notice are pinned and staged through
portable Python tooling. Source extraction tests cover hash/size mismatch,
path escape, aliases and case collisions. The [Bionic build](bionic-build.md)
compiles and combines 183 Bionic source/generated units, 23 Scudo/GWP-ASan units,
14 AOSP Arm string units and one property-info parser
in upstream and native profiles. The
native overlay redirects Bionic TLS to precompiled host endpoints and removes
Android's x18 shadow-stack ownership. Its inline signal path requires a
fixed-width raw-syscall endpoint. The host now provides that endpoint with
thread-local dispatch bindings and preserved host errno. The object passes
the checked instruction gate. Both profiles share 1,378 global definitions;
five expected weak template/TLS differences are checked explicitly. Host TLS isolation passes across
12 threads, but guest TLS construction and binding are not integrated yet.
All 216 table-generated syscall entries, their 13 aliases and the generic entry
now call the raw endpoint in the native profile. Native Linux ARM64 passes
8,280 capture cases and five smoke cases per profile using the actual NDK-built
entries and Bionic errno helper. An exported entry does not imply that its
syscall is implemented. The native partial object has 21 unresolved dependencies (18 strong).
These expanded source-build results pass locally; their CI verification is pending.
Complete Bionic startup and dynamic execution remain unimplemented. The host
dispatch test covers 12 threads, nested bindings and the five M1 syscalls;
it does not add syscall semantics or initialize guest TLS.
The allocator build retains AOSP Scudo and GWP-ASan. GWP-ASan's platform TLS hook
uses initialized guest-owned state through Bionic's native-bridge slot, avoiding
initial-exec ELF TLS. Its Linux oracle passes 8,192 exchanges per profile across
eight native threads, using the exact NDK-built caller objects.
The portable M2 mapper now supports Scudo-shaped anonymous reservations, partial
protection/unmap, fixed commit inside owned ranges and immediate zero-on-discard.
Windows, macOS and Linux CI pass all 11 CTest contracts, including 1,024 concurrent
mapping lifecycles and three actual protection faults. The identical NDK memory
caller passes 35 cases in the signed macOS Bionic slice and through both Bionic
profiles on native Linux ARM64. Its macOS run takes 196 microseconds with 16 KiB
pages, including syscall bridges and byte checks. This is a correctness-fixture
timing, not allocator throughput. Actual allocator execution and TLS startup remain
open. See [memory semantics](syscalls.md#m2-memory-manager) and ADR 0016.
The current partial build includes real stdio/gdtoa and the baseline AOSP ARM64
string dispatcher. Its 35,908-case guarded-page oracle passes on native Linux
and in the signed macOS wrapper; downloaded objects and the iOS wrapper are verified.
The original and adapted signal headers pass 1,024 thread-directed deliveries
each on native Linux ARM64, including payload and errno checks. This uses the
system libc and a Linux test endpoint; it is not Bionic or Darwin signal execution.
The [M2 contract](m2-contract.md) fixes the acceptance areas before compatibility
work; complete Bionic linkage, guest TLS, broader mappings and threads are next. Current metadata and relocation fixtures execute no Android code.

## Three largest M2 risks

1. Preserving dynamic ELF addressing, relocations and dependencies in signed
   Mach-O containers without runtime text fixups.
2. Bionic TLS, reserved registers and Android/Apple calling-convention bridges.
3. Linux-compatible threads, futexes, files and mapping behavior sufficient for
   the dynamically linked NDK suite's 90% acceptance target.
