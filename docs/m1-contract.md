# M1 experiment contract (before implementation)

Build both packaging candidates from the same original, libc-free static
AArch64 Android ELF using pinned NDK r28c (28.2.13676358). The original ELF
uses Linux `_start` and actual `svc #0` sites, calls mmap/mprotect/munmap/write/
exit, and must also run unchanged on a native Linux AArch64 CI runner. It is
not a Bionic-linked executable; Bionic remains a required M2 deliverable.

The converter will emit a Mach-O dylib with 16 KiB-aligned signed text and
export metadata. The wrapper will embed the same transformed bytes in an
Apple-linker-produced framework. Both preserve the ELF image's PC-relative
distances. Text mutation is confined to the host build tool, before signing;
runtime code only opens bundled signed frameworks via dyld.

For this fixture, each svc is replaced with a branch to a build-time veneer
that preserves the Linux general-register contract and calls the fixed-arity
syscall dispatcher. Host entry reserves x27 for the guest context and x28 for
the dispatcher; inputs using those registers, Apple's reserved x18, TLS/system
registers, unsupported control flow or SIMD are rejected by this initial
instruction subset. These restrictions must not be called general ELF/NDK
support. No runtime SVC trapping, generated executable memory or CPU emulator.

The portable parser validates segment/table bounds, alignment, overlap and
permissions before any conversion. Dynamic dependencies, TLS, dynamic
relocations and constructors are explicitly unsupported here. They remain
required Layer 0/M2 work, not silently ignored features.

Guest mappings are non-executable and owned by a guest context. Initially
support whole anonymous/private mappings and whole-mapping protection/unmap;
document policy/subset differences from Linux separately from matching Linux
semantics. A guest exit unwinds to a saved host context, never UIApplication.

Validate portable behavior on Windows, differential syscall results on Linux,
native execution of both signed frameworks on ARM64 macOS, and real arm64 iOS
compilation/signing. Device execution remains unverified until there is actual
evidence; the user waived M0's manual check. Record conversion size/time and
paired native-call/load measurements before selecting a packaging route.
