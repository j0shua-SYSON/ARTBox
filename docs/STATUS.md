# ARTBox status

M0 implements a portable startup contract and an iOS 15+ UIKit log console.
Host tests verify exactly `ARTBox ready` and exit code 0. Python tooling builds
and tests on Windows, macOS and Linux. The iOS workflow builds the real arm64
device target, verifies its transport signature and emits an IPA with provenance.

The revised cross-platform tooling is awaiting CI on the rewritten history.
See [M0 PR #1](https://github.com/j0shua-SYSON/ARTBox/pull/1).
The project owner waived M0's manual device gate. Physical-device launch and
lifecycle behavior remain unverified; no device pass is claimed.

The [loader comparison](loader-design.md) was written before loader code.
M1 will compare two signed packaging prototypes using the same static NDK ARM64
ELF and five translated syscalls. No Android ELF, Bionic, ART, Binder or APK runs.
No native-Android performance has been measured.

The three largest M1 risks are ELF addressing inside signed Mach-O containers,
Android/Apple reserved-register and TLS differences, and the larger syscall
surface needed by Bionic after the initial libc-free NDK fixture.
