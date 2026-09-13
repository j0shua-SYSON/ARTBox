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
format. Reading a relocation table's extent does not apply or validate each
relocation operation; that is the next loader step.

## Evidence

Portable CTest cases cover shared-object ranges and malformed dynamic metadata.
`scripts/test_dynamic.py` builds original NDK shared-object fixtures with a
dependency, TLS, relative data pointer and constructor. Program headers and
every dynamic symbol match LLVM for both original and sectionless copies;
the inspector also looks up each exported symbol. Malformed/unsupported
variants must fail with no partial JSON result. This is data-validation evidence,
not Bionic runtime acceptance. Raw results are saved in `m2-metadata.json`.

The layout and tag definitions follow the [System V ELF dynamic-linking
specification](https://gabi.xinuos.com/elf/08-dynamic.html) and the [Arm ELF64
ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst).
The implementation is original ARTBox code; no reference loader was imported.
