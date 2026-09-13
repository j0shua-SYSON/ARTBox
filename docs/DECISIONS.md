# Architecture decisions

## ADR 0001 - Cross-platform build tooling (accepted, M0)

Use Python's standard library for build orchestration, validation, packaging and
environment setup. CMake builds the portable core and generates the Xcode app.
Windows, macOS and Linux share the same entry point. Build/cache paths are configurable; existing user SDK and credential settings
are inherited. Keep host-specific policy, scripts and notes in ignored `work/`.
See [building](building.md).

## ADR 0002 - Small C platform boundary (accepted, M0)

The portable core emits length-delimited messages through a synchronous
caller-owned callback. The host writes stdout; UIKit copies messages before
dispatching to the main queue. No global logger, platform headers or UIKit
objects enter the core.

## ADR 0003 - CMake generates the Xcode app (accepted, M0)

Use one CMake definition for portable C and a conditional UIKit app target.
Target arm64/iphoneos/iOS 15 or later. Build Release with signing disabled before
explicit ad-hoc transport signing. Generated projects give Mac users a normal
Xcode provisioning entry point. The bundle identifier is configurable.

## ADR 0004 - Record build and execution evidence separately (accepted, M0)

Host CI, iOS compile/sign verification, and physical-device execution establish
different things. An empty-entitlement `codesign -s -` IPA is a transport
artifact, following S5LBox's build pattern. Record each milestone's CI results,
measurements and device status before tagging. The project owner waived M0's
manual device gate; this permits M1 work after green M0 CI without claiming
physical-device execution. Later device evidence remains an explicit requirement.

## ADR 0005 - Signed native packaging (proposed, M1)

The pre-implementation [loader comparison](loader-design.md) recommends prototyping
both approaches against identical ELF input. Initially prefer an Apple-linker
wrapper for a controlled source-built ELF; measure before selecting. Signing
bytes does not adapt Android TLS, reserved registers or the function ABI.
No writable/executable mappings or runtime instruction mutation. Unsupported
post-install native code must fail explicitly.

## ADR 0006 - Scope and license boundaries (accepted, M0)

M0 contains no AOSP. Inspect and pin only the components required for the current
milestone. Dependencies retain their actual licenses, including Bionic's BSD
notices. No GPL reference implementation is copied into the MIT core. No GMS,
vendor blobs, guest kernel, CPU interpreter or JIT is part of ARTBox.

## ADR 0007 - Python for build-time packaging (accepted, M1)

The packer is a Python standard-library host tool. It validates a controlled
static ELF, replaces syscall sites before signing, and emits both a direct
Mach-O dylib and an assembly wrapper containing identical transformed bytes.
The runtime parser, syscall dispatcher and memory interface remain portable C.
The host packer's stricter input validation is independent of the runtime parser;
neither is a general dynamic linker yet.

Keep code and read-only constants at their original relative addresses in one
Mach-O text segment. The wrapper uses a regular section in `__TEXT` because it
contains both instructions and constants. LLVM rejected `some_instructions` as
an assembly section attribute; segment protection, not that annotation, controls
execution. Local LLVM inspection validates the container/export layout and
generated veneers. Only native dyld execution can establish that either route
works, so the packaging recommendation remains provisional.

The instruction allowlist enforces this fixture's ABI contract. It does not
establish isolation from malicious native code inside the host process.

## No-JIT cost ledger

| Constraint | Consequence / evidence |
| --- | --- |
| Prepare native code before signing | New native libraries need conversion, rebuild and re-sign; unimplemented. |
| No ART JIT or runtime code patching | Use the DEX interpreter or signed host-produced AOT; slowdown not measured. |
| No executable anonymous mappings | Guest executable `mmap`/`mprotect` requests fail explicitly. |
| Fix ABI/TLS/syscalls before install | Native input compatibility is constrained and must be tested. |
| AOT depends on ART/boot image/compiler versions | Pin the complete AOT provenance. |
| No runtime-generated native trampolines | Use precompiled ABI bridges or supported DEX interpreter paths. |
