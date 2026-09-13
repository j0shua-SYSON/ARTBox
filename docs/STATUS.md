# ARTBox status

M0 provides a portable startup contract and an iOS 15+ UIKit log console.
Host tests verify exactly `ARTBox ready` and exit code 0. The iOS workflow builds
the real arm64 device target, validates its transport signature and emits an IPA.
Python automation passes Windows, macOS, Linux and iOS CI. The downloaded
iOS 15 IPA matches its manifest; see [M0 acceptance](acceptance/m0.md).

M0's physical-device check was waived by the project owner. Device execution
remains unverified; the waiver is not a passing device result. The milestone PR
and its CI results are tracked in [PR #1](https://github.com/j0shua-SYSON/ARTBox/pull/1).

M1 is in progress. Static ELF validation and five syscall contracts have portable
tests. The Python packer emits both Mach-O conversion and linker-wrapper inputs
from the same static NDK ELF. Local format checks and LLVM assembly/disassembly
pass; Apple signing and native execution of these prototypes remain unverified.
Dynamic Bionic, ART, Binder, graphics and APK
execution remain future milestones. The [loader design](loader-design.md) was
recorded before loader implementation.

## Next verification

1. Sign and execute the same static NDK ARM64 ELF through both packaging prototypes.
2. Compare native Apple execution with unchanged execution on ARM64 Linux,
   record five-syscall results and measure packaging and runtime costs.

## Three largest M1 risks

1. Preserving ELF addressing inside signed, dyld-compatible Mach-O containers.
2. Android/Apple ABI differences, particularly reserved registers and TLS.
3. A normal Bionic-linked executable requires more than the initial five calls;
   the libc-free NDK fixture does not establish Bionic compatibility.
