# ARTBox status

M0 provides a portable startup contract and an iOS 15+ UIKit log console.
Host tests verify exactly `ARTBox ready` and exit code 0. The iOS workflow builds
the real arm64 device target, validates its transport signature and emits an IPA.
The revised Python automation is being validated across Windows, macOS and Linux.

M0's physical-device check was waived by the project owner. Device execution
remains unverified; the waiver is not a passing device result. The milestone PR
and its CI results are tracked in [PR #1](https://github.com/j0shua-SYSON/ARTBox/pull/1).

M1 is in progress. Static ELF validation and five syscall contracts have portable
tests. The paired signed-container prototypes and native Android execution are
not yet implemented or measured. Dynamic Bionic, ART, Binder, graphics and APK
execution remain future milestones. The [loader design](loader-design.md) was
recorded before loader implementation.

## Next verification

1. Validate the cross-platform Python build entry point and retrieve the iOS 15 IPA.
2. Run the same static NDK ARM64 ELF through both signed packaging prototypes.
3. Compare native Apple execution with unchanged execution on ARM64 Linux,
   record five-syscall results and measure packaging and runtime costs.

## Three largest M1 risks

1. Preserving ELF addressing inside signed, dyld-compatible Mach-O containers.
2. Android/Apple ABI differences, particularly reserved registers and TLS.
3. A normal Bionic-linked executable requires more than the initial five calls;
   the libc-free NDK fixture does not establish Bionic compatibility.
