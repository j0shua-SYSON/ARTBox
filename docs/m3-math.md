# M3 source-built math dependency

ART's current Android build imports 20 math entry points absent from the M2
Bionic subset. Build those functions from the pinned Android 15 Bionic libm
and ARM optimized routines. Keep the original algorithms and AOSP compiler
settings, including the BSD no-errno policy. Do not forward Android long double
arguments into Apple's different ABI.

The selection contains 28 Bionic units and seven ARM units. Android compiler-rt
provides binary128 arithmetic used internally by the BSD implementation. The
result is a `libm.so` subset with no external imports; it is not a complete
Android math library. The separate original NDK client imports all 20 functions
through `DT_NEEDED`. Its 78 vectors cover ordinary values, signed zeros,
NaN/infinity, subnormal boundaries and large trigonometric arguments. Normal
transcendental results allow two ULP; exact cases require identical bits.
This is not exhaustive accuracy, errno or floating-point-environment coverage.

`scripts/test_art_math.py` builds both libraries, checks their instructions and
imports, and prepares the existing signed wrapper layout. On ARM64 macOS it
signs both frameworks, validates their original ELF bytes, relocates them as one
dependency group and executes the client through the fixed-width call bridge.
The same script builds and verifies iOS 15 frameworks without claiming device
execution. Native Linux runs the identical Android ELF pair and a separately
compiled system-libm reference against the same vectors.

Each artifact retains exact source/object/binary hashes, original selected
sources and headers, the client, build inputs and full Bionic libm/libc, ARM,
NDK and ARTBox notices. `third_party/bionic/art-math.json` records the source
selection. The corresponding-source archive accompanies the temporary
framework artifacts. No math source is adapted.

Status: local Android compilation and dependency closure pass; signed Mac and
native Linux execution are pending CI. This dependency does not establish ART
startup on Apple platforms, and the integrated M2 IPA does not yet contain it.
