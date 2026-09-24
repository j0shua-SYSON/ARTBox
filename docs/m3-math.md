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

At `236625c`, all 78 cases pass in signed Mac code, in the identical Android
libraries on native Linux ARM64, and in the Linux system-libm reference.
[Host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35967315976) and
[iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35967315901)
both pass. Local verification of the downloaded evidence checks 21 canonical
project files, 288 original source/header files, all 35 objects, both ELFs,
four signed framework layouts and 20 included notice hashes. The native logs
and Linux report match the producer's exact report hash.

The Mac-produced `libm.so` is 61,992 bytes and contains 6,075 instructions;
its separate client is 19,112 bytes and contains 185 instructions. Neither
contains direct syscalls, thread-pointer accesses or x18/x27/x28 use. These
are build-size observations, not a math throughput measurement. Signature
validation runs on macOS; local verification independently checks the retained
Mach-O layout, payloads and hashes. Device execution remains unverified.

This dependency does not establish ART startup on Apple platforms, and the
integrated M2 IPA does not yet contain it.
