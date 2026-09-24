# LLVM register-context boundary

The pinned NDK unwinder's ARM64 context restore loads both `x18` and `x19`
from saved storage. ARTBox reserves the live Apple platform register `x18`.
ART's own long jump already skips that register, but an NDK static dependency
can introduce a separate write even when every C++ unit reserves it.

Use the original LLVM context assembly at the base revision named by NDK r28c.
Before adapting it, reassemble its save and restore routines and require their
text sections to match the NDK archive members byte for byte. Change the one
paired `x18`/`x19` load to an `x19` load at the original `x19` offset. Keep the
remaining register layout, stack restoration and jump code. Reject changed or
already adapted source by hash and unique textual context.

The original ARTBox assembly fixture calls the real save function, changes the
saved `x18` to the complement of its live value, and supplies known `x19` and
`d8` values and a continuation address. Its failure mask checks preservation of
live `x18`, restoration of `x19` and `d8`, and the captured stack/frame state.
The unchanged restore must fail only the `x18` check on native Linux; the
negative-control fixture repairs `x18` before returning to its Linux caller.
That negative control is never packaged for Apple execution.

The adapted ELF has no imports or runtime code edits. Its wrapper is signed for
ARM64 macOS and iOS 15; the Mac runner verifies the original ELF bytes, uses the
portable relocation engine and calls the entry through the platform bridge.
Native Linux runs both original and adapted ELFs and compares the latter's
result to the Mac result. Original source, LLVM license, the adapted assembly,
test sources, object hashes and source-equivalence results accompany the fixture.

```console
python -B scripts/test_unwind_context.py
```

The builder is cross-platform; signed execution requires macOS/Xcode.
This tests the context-transfer boundary only. The complete guest ART link,
general C++ exception unwinding and ART execution on Apple remain incomplete.

## Verified execution at bba15cd

[Host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35962273330) and
[iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35962273333)
pass. The original Linux control reports failure mask **1**, identifying only
the poisoned x18 restore. Both the adapted Linux ELF and signed Mac wrapper
report mask **0**, with four checks and successful cleanup.

Downloaded artifacts verify 17 canonical project files, four pinned LLVM files,
the source adaptation, both ELF/fixture hashes and both source-equivalent NDK
members. In this build, the complete original object files also match the NDK
members, beyond the required text-section equality. The Mac and iOS framework
layouts and license hashes verify against their CI signing records; both use
empty entitlements. The Linux result references the exact Mac producer report.
The tested merge `c8cef34cb0b23d75c97074742fa4d813ffb17218` contains branch head
`bba15cd5486e6291b61e8b80836646561d4a3af2`. Physical execution remains unverified.
