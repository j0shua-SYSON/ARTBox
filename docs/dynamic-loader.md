# Dynamic ELF implementation

The M2 C reader accepts little-endian AArch64 ET_EXEC and ET_DYN files. It checks
program-header bounds, PT_LOAD ranges and permissions, TLS templates, RELRO,
interpreter strings and file-backed virtual views. The original M1 validation
entry keeps its static-only contract. These operations do not map executable
memory or invoke guest code.

`artbox_dynamic_open` reads PT_DYNAMIC without using section headers. It checks
string/symbol tables, ordered DT_NEEDED names, SONAME, search-path metadata,
RELA/PLT-RELA/RELR table extents, initializer/finalizer tables and dynamic flags.
GNU hash and SysV hash support symbol enumeration and lookup. Hash chains,
bloom membership, symbol names and counts are bounded and checked; malformed
tables cannot leave a partially published metadata view.

Lookup returns a defined, externally visible ELF symbol value and type. A
result can describe a function, TLS symbol, object, absolute value or IFUNC;
the caller must apply the corresponding runtime semantics. It is not a host
function pointer. No dependency graph, IFUNC invocation, TLS allocation or
constructor execution is implemented by this metadata API.

The initial implementation explicitly rejects symbol versions, REL and
Android packed REL/RELA, text relocations, and AArch64 variant-PCS symbols.
Standard RELR and the earlier Android RELR tag aliases share the same table
format.

`artbox_relocate` applies AArch64 ABS64, GLOB_DAT, JUMP_SLOT, RELATIVE and RELR
to caller-owned writable PT_LOAD views. It decodes RELR bitmaps, resolves symbol
references, and prepares every write before changing any destination. Errors
leave the data and output statistics unchanged. Text targets, source-buffer
aliases, missing mappings and out-of-range targets fail. Overlapping/composed
relocations, TLS, IFUNC, COPY and instruction relocations remain unsupported.

Local, hidden, internal and protected definitions bind within the image.
Default-visible definitions are offered to the caller's resolver for
interposition; undefined weak references become zero, while unresolved strong
references fail. The callback supplies the lookup scope; a process namespace
and dependency graph are still required. PLT slots resolve eagerly. Byte stores
support unaligned targets, and 64-bit address arithmetic retains the low 64
bits as prescribed by the Arm ABI. A call expects fresh data: applying RELR
twice would add the bias twice.

Writable views can be staging buffers. This API does not prove that a signed
Mach-O package preserves the ELF load bias, make code executable, apply RELRO,
invoke constructors or run Bionic. Those are separate integration steps. Its
temporary plan is bounded to one million entries, counting each RELR word as
up to 63 writes for allocation; larger inputs report unsupported.

## Evidence

Portable CTest cases cover shared-object ranges and malformed dynamic metadata.
`scripts/test_dynamic.py` builds original NDK shared-object fixtures with a
dependency, TLS, relative data pointer and constructor. Program headers and
every dynamic symbol match LLVM for both original and sectionless copies;
the inspector also looks up each exported symbol. Malformed/unsupported
variants must fail with no partial JSON result. This is data-validation evidence,
not Bionic runtime acceptance. Raw results are saved in `m2-metadata.json`.

`scripts/test_relocation.py` builds another original NDK fixture in RELA and
RELR forms. LLVM expands its 136 relocations, including 130 relative pointers,
GOT/PLT entries, an imported data pointer and an unresolved weak reference. The
test compares every writable segment byte after applying ARTBox relocations,
for both original and sectionless inputs. It also checks that original ELF
bytes remain unchanged. Results are in `m2-relocations.json`; this is data-only
evidence, with no guest execution. Portable tests additionally cover late
binding failure, protected/local/absolute symbols, negative addends, bitmap
boundaries, overlapping destinations and attempts to relocate signed text.

The layout and tag definitions follow the [System V ELF dynamic-linking
specification](https://gabi.xinuos.com/elf/08-dynamic.html) and the [Arm ELF64
ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst).
RELR decoding follows the [generic ELF relocation
format](https://gabi.xinuos.com/elf/06-reloc.html).
The implementation is original ARTBox code; no reference loader was imported.
