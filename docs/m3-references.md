# M3 reference adaptation evidence

This is a prerequisite result, not M3 acceptance. It executes compiled AOSP
reference types, not the ART runtime, a collector or DEX instructions.

- Implementation: `2586a500ffd9898b2a47e399d202a23265b03bce`.
- [Host, original Linux ARM64 and integrated iOS regression CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34835737111).
- [Independent iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34835737117).
- Downloaded evidence: `artifacts/m3-2586a50/host` and
  `artifacts/m3-2586a50/linux`. Reports preserve tested merge/source hashes,
  exact dependency selections, object/ELF hashes and native results.

The same original NDK C++ contract exercises AOSP's `ObjectReference`,
`HeapReference` and `CompressedReference` with heap poisoning disabled and
enabled. Each profile passes 19 cases on both native systems. The original
Linux objects use absolute references into a reservation at 1 GiB. The signed
Mac objects use the checked heap-relative bridge with reservations at
4,345,069,568 and 4,300,668,928 bytes, respectively.

| Mac storage observation | Plain | Poisoned |
| --- | ---: | ---: |
| Poison flag | 0 | 1 |
| First heap reference | 16,384 | 4,294,950,912 |
| Second heap reference, volatile store | 32,768 | 4,294,934,528 |
| Second stack reference | 32,768 | 32,768 |

The host checks those words independently. Original and adapted binaries each
retain two ARM64 acquire loads and two release stores, and their instruction
inventory contains no SVC, TPIDR access, reserved-register use or unknown
instruction. Only the adapted objects import the two reference bridges. Reads
and writes through decoded references reach the expected native storage; both
processes release their test mappings.

The first fixture at `89aa2b7` passed but allowed the optimizer to remove most
local storage and make both poison profiles identical. That evidence alone did
not establish executed volatile accesses. The strengthened fixture materializes
the heap object in caller-provided aligned bytes, observes representations
through volatile byte reads and checks the generated acquire/release operations.

The downloaded four framework binaries match their manifests and original ELF
code/data bytes. They have ARM64 Mach-O headers, the expected macOS 11/iOS 15
deployment commands, ad-hoc signatures, empty entitlements and no writable
executable segments. CI runs strict signature verification; downloaded notices
and source hashes are checked again locally. iOS frameworks are build artifacts;
they are not yet integrated into an ART execution entry or physically launched.

No isolated performance benchmark was taken. Bridge calls and checked
add/subtract/range operations add overhead that must be measured with the actual
interpreter. `ObjPtr` debug encoding, CAS, GC roots, read barriers, class layout,
managed stack walking, JNI and AOT accesses remain unvalidated.
