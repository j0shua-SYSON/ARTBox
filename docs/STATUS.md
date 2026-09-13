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

The Apple-linker wrapper is selected for M2 based on the measured M1 experiment;
the direct converter remains a comparison prototype. The [loader design](loader-design.md)
was recorded before implementation. Dynamic Bionic, ART, Binder, graphics and
APK execution remain future work.

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
see [dynamic loader details](dynamic-loader.md). The executable packer still
targets the M1 static fixture.

The Bionic Android 15 source archive and notice are pinned and staged through
portable Python tooling. Source extraction tests cover hash/size mismatch,
path escape, aliases and case collisions. No Bionic runtime executes yet.
The [M2 contract](m2-contract.md) fixes the acceptance areas before compatibility
work; signed dynamic packaging, symbol versions and Bionic build dependencies
are next. Current metadata and relocation fixtures execute no Android code.

## Three largest M2 risks

1. Preserving dynamic ELF addressing, relocations and dependencies in signed
   Mach-O containers without runtime text fixups.
2. Bionic TLS, reserved registers and Android/Apple calling-convention bridges.
3. Linux-compatible threads, futexes, files and mapping behavior sufficient for
   the dynamically linked NDK suite's 90% acceptance target.
