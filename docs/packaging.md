# M1 packaging prototype

`tools/pack_elf.py` is a build-time Python tool. It emits unsigned inputs that
must be linked and signed with Apple tools before native execution. It never
maps executable memory or modifies installed code.

The current fixture is an original, libc-free static ARM64 Android ELF compiled
with NDK r28c. It uses `mmap`, `mprotect`, `munmap`, `write` and `exit`. A successful
path invokes five syscalls; the sixth static SVC site is the failure exit.

## Input contract

One 16 KiB-aligned read/execute PT_LOAD, an entry at the start of `.text`, and
read-only constants at preserved relative addresses. Retained RELA entries must
describe already-resolved `R_AARCH64_ADR_PREL_LO21` references within this image.
Dynamic dependencies, TLS, constructors and other relocations are unsupported.

The current general-register instruction subset rejects x18, x27, x28, SIMD,
system-register access, indirect returns and branches outside the audited text.
The last instruction must prevent fallthrough into constants. This constrains
the fixture's ABI; it is not a security sandbox for malicious native code.

Each SVC becomes a branch to a prebuilt 128-byte veneer. The veneer saves Linux
caller-visible GPRs and NZCV, supplies the guest context and syscall arguments
to a fixed-arity C dispatcher, restores state and branches back. Guest exit
returns to the saved host context without terminating the app process.

## Outputs and local verification

```sh
python tools/pack_elf.py build/m1/hello.elf build/m1/pack macos
python tests/check_pack.py tools/pack_elf.py build/m1/hello.elf build/m1/pack-tests
```

Use `ios` as the final packer argument for the iOS 15 container. Outputs include
`payload.bin`, `converted.dylib`, `wrapper.S`, `guest_image.h`, and a JSON manifest
with input hash, sizes, patch locations and packaging duration. The wrapper's
assembler include path must contain `payload.bin`.

The current fixture has 193 image bytes and produces a 964-byte payload including
six veneers. The direct unsigned dylib is 49,152 bytes, with 16 KiB text and
link-edit alignment and one exported guest entry. These are format measurements,
not native execution or signing results.

Local tests verify retained instruction bytes, branch destinations, Mach-O
segments and rejected malformed/unsupported inputs. NDK LLVM independently
decodes the export trie and generated instructions, and assembles the wrapper
as an arm64 iOS 15 Mach-O object. Apple linker, codesign, dyld and native execution
validation remain required before choosing a packaging approach.
