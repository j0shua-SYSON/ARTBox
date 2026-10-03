# Interpreter arguments above 4 GiB

Changing ObjectReference also changes the raw reference values stored in
ShadowFrame virtual registers. The pinned interpreter's AssignRegister compares
that value with the low 32 bits of a native pointer. A heap-relative value can
therefore be copied as an integer, clearing the callee's reference slot and
losing the root needed by a moving collector.

The adaptation compares the vreg with the checked encoding of the reference
slot. It preserves the original distinction between a live reference and a
primitive beside a stale reference. The source hash and unique replacement
context are checked before constructing a separate source tree.

The test includes the actual AOSP `interpreter_common.cc`, with ShadowFrame,
AssignRegister and both CopyRegisters specializations. It does not duplicate
those helpers. All code in that translation unit compiles; section collection
then retains only the exported test and the helpers it reaches. The final ELF's
imports and instructions are checked before signing. Frame destruction uses the
host's real C++ deallocator; no allocated STL object crosses the ABI boundary.

Each plain/poisoned profile has 18 cases covering arbitrary-order and contiguous
argument copies, nulls, stale references, primitives overwriting references,
wide values, both reference copies and writes through decoded callee roots.
Stack references remain unpoisoned in both heap-poisoning profiles.

Three builds distinguish the behavior:

- Original AOSP runs below 4 GiB on native Linux ARM64.
- A reference-codec-only control runs in signed Mac code above 4 GiB and must
  fail exactly at case four, before writing to the supplied object storage.
- The reference codec plus argument-copy adaptation must pass all cases in
  signed Mac code. The same payload also builds as a signed iOS 15 framework.

The Mac heap base is deliberately not aligned to 4 GiB: such an alignment would
make truncated native pointers accidentally agree with byte offsets.

```console
python -B scripts/test_art_stack.py
```

First build the host runner with `scripts/build.py host` on ARM64 macOS. Linux
consumes the Mac host artifact with `--evidence-root PATH`. Windows can compile
and inspect all six ARM64 payloads.

## Verified argument copying

At `5472832789c328cb1f45f04c965c7a7f4b5c20f4`, the Mac and native Linux jobs in
[this CI run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35950207574)
each pass 36 positive cases. Both partial-adaptation Mac controls return exactly
`-4` with untouched output/object storage. The adapted Mac bases are 4,330,307,584
and 4,329,340,928; Linux uses 1,073,741,824. Both profiles preserve unpoisoned
reference words in the callee's stack slots.

Downloaded Mac artifact `10788416724` has SHA-256
`064e378868ff81e08e9e55345c60f3045f3e2a9524685f3d60b04dab6da6bcfe`;
Linux artifact `10788042876` has SHA-256
`bdbe6278e3fcc2581db9c18bfb0cf7c7f047a178299c320df4c9e5a9ed4af187`.
The reports identify tested merge `7f12b864fa6030e61dfbd1abd90e27df85fc7d6e`,
whose parents include that code revision. Verification covers 28 canonical
project inputs, 1,003 original source entries, the source archive, both overlay
variants, all 12 object/ELF hashes and six signed framework binaries. Mac CI
verifies signatures and empty entitlements. The two iOS 15 frameworks remain
build artifacts. No interpreter-throughput or collector timing was measured.

These tests do not start ART, execute DEX or run a collector. Full-runtime heap
allocation/card coverage, other raw-reference call paths, native transitions
and thread/signal integration remain M3 work. The checked codec adds calls on
the argument-copy path; interpreter cost must be measured in the complete runtime.
