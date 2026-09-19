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
claiming native execution. Native validation of this new contract is pending.

Full interpreter argument passing, stack walking, debug pointer cookies, heap
allocation/card coverage, native entrypoints and a moving collector in a high
heap remain separate requirements. The existing original Linux ART managed
acceptance suite stays unchanged.
