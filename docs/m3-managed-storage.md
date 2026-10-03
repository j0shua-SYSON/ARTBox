# Managed storage above 4 GiB

This test compiles actual pinned AOSP LockWord, GcRoot, LrtEntry and HeapReference
types. It is preparation for the full runtime's address representation, not ART
startup, collection or managed execution on Apple.

The 27-case contract checks forwarding words and their preserved state bits,
ordinary thin locks and hash words, GC roots, JNI reference/free/serial/dead entry
transitions, poisoned and plain heap references, and reads and writes through
decoded roots. External storage and volatile observations retain real memory
operations. Both heap-poisoning profiles must pass.

The unmodified forwarding word truncates pointers to 32 bits. Its signed Mac
control must fail specifically at the first high-address round trip, before
touching the supplied object storage. The adapted Mac payload must pass all
cases above 4 GiB; the original, byte-identical NDK ELF must pass below 4 GiB on
native Linux ARM64. The signed iOS frameworks are inspected build artifacts.

The adaptation uses the existing checked byte-offset codec for forwarding words
as well as references. JNI's removed-entry value `0xdead10c0` remains a raw marker
through a dedicated setter; the codec still rejects pointers outside its heap
window. All three original removal/pruning sites use that setter. Ordinary lock,
hash, free-list and serial-number encodings remain under test.

Original files are hash checked before any copy is changed. A separate complete
ART source selection prevents quoted includes from finding adjacent unmodified
headers. Source bundles retain original and adapted files, project inputs and
component notices. The fixture introduces no generated native code, syscall,
native TLS access or reserved-register dependency.

```console
python -B scripts/test_art_storage.py
```

On ARM64 macOS, first build the host targets with `scripts/build.py host`.
The Linux comparison consumes the Mac host-evidence artifact through
`--evidence-root PATH`. Windows can build and inspect the ARM64 inputs without
claiming native execution.

## Verified storage contract

At `773da403fedfa040cbd73f72830d978c6851ef0a`, the
[signed Mac job](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35444771156/job/105901695666)
and native Linux comparison pass: **54 positive cases on each host**, plus two
expected Mac rejections. Both plain and poisoned references pass. The Mac heap
bases are 4,368,384,000 and 4,329,439,232; Linux uses 1,073,741,824. Each original
signed Mac control returns exactly `-5` before writing to the supplied storage.

The adapted ELF grows from 17,880 to 18,232 bytes. Instruction counts change from
331 to 390 in the plain profile and from 336 to 396 in the poisoned profile.
Each payload retains two acquire loads and two release stores. These are code
size observations; no collector or interpreter timing was measured by this test.

The downloaded Mac artifact is `10584248581`, SHA-256
`db2053a05e15416a8ddef4959a0c8b0653e7f5129256a1bea1f4421cc0fb2e9c`.
The Linux artifact is `10584822960`, SHA-256
`413d85e26bd7b5f8c100989cd50977be9e4eaf7adeae0def13923ed57277b942`.
Their reports identify tested merge `b77715455f821a583ce63a840f37624a049ee588`,
whose parents include the code revision above. Verification covers 26 canonical
project inputs, 999 original source entries, five original/adapted file pairs,
all eight object/ELF hashes, the source archive and six framework binaries.
Mac CI verifies signatures and empty entitlements. The two iOS frameworks target
arm64 iOS 15; they have not been executed on an iPhone or integrated into ART.

Full interpreter argument passing, stack walking, debug pointer cookies, heap
allocation/card coverage, native entrypoints and a moving collector in a high
heap remain separate requirements. The existing original Linux ART managed
acceptance suite stays unchanged.
