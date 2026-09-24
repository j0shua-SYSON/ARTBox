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

The builder is cross-platform; signed execution requires macOS/Xcode. Windows
compilation and source/machine-code equivalence pass; native CI is pending.
This tests the context-transfer boundary only. The complete guest ART link,
general C++ exception unwinding and ART execution on Apple remain incomplete.
