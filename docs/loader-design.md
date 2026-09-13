# Native ARM64 loading under iOS signing

Status: design recorded before loader implementation; experiment results and
selection added at M1. See [M1 acceptance](acceptance/m1.md) for measured evidence.

## Recommendation

Prototype both routes against the **same controlled static NDK ARM64 ELF**.
Prefer a linker-produced signed Mach-O wrapper for initial bring-up: Apple
tools own the container and signature, and ARTBox exposes one narrow guest
entry. Keep full ELF-to-Mach-O conversion as the generalization candidate.
That initial recommendation is now accepted for M2. Both prototypes execute
on the ARM64 Apple host and produce signed iOS 15 frameworks. The wrapper is
smaller for this fixture and has no observed invocation penalty. Physical-device
checks were waived for all milestones; iPhone execution remains unverified.

| | Build-time ELF-to-Mach-O conversion | Build-time signed wrapper |
| --- | --- | --- |
| Executable bytes | Copy audited ELF code into Mach-O executable segments before signing | Emit audited bytes as executable Mach-O object sections; link into the app/framework before signing |
| Layout | Preserve ELF virtual-distance relationships; translate segments, exports and fixups | Let ld64 form a valid container; explicitly relocate/rewrite addresses or constrain the fixture's layout |
| Runtime | dyld maps signed text; ARTBox resolves guest symbols and writable data | A compiled entry veneer reaches linked signed text; ARTBox resolves writable data |
| Main cost | Mach-O/dyld correctness, 16 KiB page layout, fixup translation and metadata | Reconstructing missing ELF relocations and preserving cross-segment PC-relative references |
| First useful scope | General ELF segment/relocation corpus, then Bionic | Controlled source-built static ELF with retained relocations and audited instructions |
| Invalid shortcut | Converting a resource at runtime, then trying to execute it | A signed stub branching into unsigned ELF bytes in heap/resource data |

Both routes are native execution, with build-time transformation. Neither may
write instruction pages at runtime or request executable anonymous memory.
A wrapper is not a code-signing exception. If it must reproduce arbitrary ELF
layout and rebasing, it has acquired much of a converter's complexity.

## Contract that must be proven

1. Treat an ELF as untrusted data until a host conversion step validates it.
   Bound every ELF table, segment size, offset addition, alignment, relocation
   target, symbol index, and dependency. Reject W+X segments, executable BSS,
   unsupported relocations and instructions; preserve rejection evidence.
2. Preserve intra-image addressing across `ADRP`/`ADD`, literals, jump tables,
   GOT, RELA/RELR, and constructors. A byte array plus a function-pointer cast
   does not solve cross-segment addressing. RELA/RELR/GNU hash, TLS,
   `DT_NEEDED`, versions and constructors grow under tests in Layer 0; they
   are not M0 capabilities.
3. Build/sign with 16 KiB-compatible segment layout for real arm64 iOS 15+.
   Let dyld map text; apply only approved writable-data fixups after loading.
   Reject text fixups that cannot be resolved before signing. AOT outputs and
   native libraries supplied after installation require a rebuilt, signed
   bundle. DEX can eventually use ART's interpreter; arbitrary ARM64 machine
   code has no interpreter fallback under the no-CPU-emulation constraint.
4. Rebuild/source-patch Bionic syscall sites before signing. Use fixed-arity
   veneers to marshal Linux numbers, errors and structures; never invoke the
   same-numbered Darwin syscall. An inline `svc` elsewhere must be explicitly
   transformed or rejected. No signal-trap or exception-port interception.
5. Handle the **ABI**, not just the ISA: Apple reserves `x18`; Android TLS
   access cannot reuse Darwin's thread pointer. Bridge stack/variadic ABI
   differences, errno, host callbacks and thread lifecycle. Source builds can
   reserve registers and substitute TLS accesses; unknown native APK code
   may be incompatible. Signals can occur inside guest code, so restoring
   reserved state only at a syscall boundary is insufficient.
6. A Linux `_start` needs argc/argv/envp/auxv and a guest exit path. `exit` must
   finish the guest invocation without terminating UIApplication. Ordinary
   static Bionic startup uses more than five syscalls. Start with an explicitly
   libc-free, `-nostdlib` static ELF produced by the NDK; do not label that as
   Bionic bring-up or an unmodified normal NDK executable.

M1's planned initial calls are `write`, `exit`, `mmap`, `mprotect`, and
`munmap`, with documented subsets, Linux oracle tests, and negative cases.
Guest memory mappings remain non-executable. Dynamic Bionic belongs to M2;
the first dynamic library must be Bionic, not a replacement libc called Bionic.

## Paired experiment and decision gate

Build a pinned NDK fixture with retained relocation information and audited
code boundaries. Produce both candidate containers from the identical ELF.
Test return values, all five syscall results, text/data references, unsupported
input rejection, and explicit exit 0. Run native execution on an ARM64 Mac and
compare the original ELF on native ARM64 Linux. Build/sign the real iOS target.
Physical-device execution is optional under ADR 0004; Windows validates formats
and portable semantics.

Record input/output SHA-256, tool versions, signed binary/text/data sizes,
conversion duration, cold load-to-entry latency, peak/resident memory (with
measurement method), and repeated-call timing with count/median/range. Inspect
load commands, imports, signature, entitlements, and text permissions. No
claim of zero runtime text writes from a source scan alone. Publish failures
as well as successes. No fake performance values or skips counted as passes.

| Evidence | Converter | Wrapper |
| --- | --- | --- |
| ARM64 macOS native execution | Pass, 100 invocations | Pass, 100 invocations |
| iOS 15 build and signature verification | Pass | Pass |
| Physical-device execution | Unverified; gate waived | Unverified; gate waived |
| Conversion time / size / load latency / memory | Recorded in [M1 evidence](acceptance/m1.md) | Recorded in [M1 evidence](acceptance/m1.md) |

The wrapper is selected for further development, with the physical execution
gap recorded. Generalizing either route still needs dynamic ELF and ABI work;
the static fixture does not establish Bionic or APK compatibility. Unsupported
native inputs remain explicit errors. A future DEX interpreter path must not
introduce JIT, CPU emulation, kernel boot, or entitlement workarounds.

## Sources and implications

- [Apple runtime security](https://support.apple.com/guide/security/security-of-runtime-process-sec15bfe098e/web)
  and [code signing](https://support.apple.com/guide/security/app-code-signing-process-sec7c917bf14/web)
  establish the executable-code boundary. The proposed packaging is an
  inference to validate on device, not an Apple promise about converted ELF.
- [Apple ARM64 ABI](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms)
  documents reserved registers and calling-convention differences.
- [Bionic overview](https://android.googlesource.com/platform/bionic/+/refs/tags/android-15.0.0_r1/README.md)
  describes syscall stub generation and libc/linker organization. Source-level
  changes must cover TLS and bootstrap paths as well as generated stubs.
- [Waydroid](https://github.com/waydroid/waydroid),
  [Anbox](https://github.com/anbox/anbox),
  [Box64](https://github.com/ptitSeb/box64),
  [FEX](https://github.com/FEX-Emu/FEX), and
  [Darling](https://github.com/darlinghq/darling) inform boundaries, not proof
  of iOS feasibility. Waydroid/Anbox retain a Linux kernel; Box64/FEX translate
  CPUs; Darling's userspace server targets Linux rather than the iOS sandbox.
- [ART](https://source.android.com/docs/core/runtime),
  [Binder](https://source.android.com/docs/core/architecture/hidl/binder-ipc),
  [SurfaceFlinger](https://source.android.com/docs/core/graphics/surfaceflinger-windowmanager),
  and [ANGLE](https://chromium.googlesource.com/angle/angle/+/main/README.md)
  were surveyed only. ART's Linux integration, Binder driver semantics and
  BufferQueue/fence ownership remain later work. No AOSP checkout is needed
  for the startup console.
