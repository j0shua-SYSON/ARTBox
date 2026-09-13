# ARTBox

An experimental native Android userspace runtime for iOS 15 and later.
Portable C/C++, signed ahead-of-time ARM64 code and a planned ART bytecode
interpreter. No CPU emulation, guest kernel, JIT or private entitlements.

The pre-implementation [loader design](docs/loader-design.md) compares build-time
ELF-to-Mach-O conversion with signed wrappers. M0 establishes the app shell and
portable startup tests before M1's native loading experiments.

Original code is MIT licensed. See [third-party provenance](THIRD_PARTY.md).
