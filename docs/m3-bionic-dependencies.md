# Bionic dependencies for ART

The M3 Android ART link needs libc entry points outside the M2 startup subset.
Extend the same source-built Bionic with 50 unchanged units from its pinned
Android 15 revision. `third_party/bionic/m2-objects.json` records these sources,
their hashes and the corresponding AOSP build flags. This brings the shared
source selection to 271 units, including its existing support components.

The added closure includes BSD sorting/string routines, locale adapters,
path/directory and syscall wrappers, system-property writing, and time-zone
code. It adds no strong host imports beyond the existing Bionic boundary.
Building these functions does not add their missing kernel behavior: directory
enumeration, filesystem mutation, process creation, property-service sockets
and signal delivery still require their own implementations and acceptance.
The syscall compatibility matrix remains authoritative for those limits.

An original NDK client checks 30 sorting, basename/dirname, C-locale collation,
integer overflow/conversion, Android long-double conversion, wide-string,
multibyte and random-sequence behaviors. It runs inside the existing signed
Bionic process in both normal and forced GWP-ASan modes. The M2 suite's fixed
328-case denominator remains unchanged; this regression has its own required
result. Native Linux compiles the same client against system libc and compares
that result with both signed runs.

The iOS diagnostic package uses the same shared runner and libraries. Staging
requires the new 30-case result as well as the existing M2 acceptance. Compiler
objects, source hashes and complete upstream notices accompany the CI evidence.
This work adds dependency coverage; it does not establish Apple ART startup.

Both 271-unit public source profiles compile and preserve 1,540 shared global
definitions. The adapted profile contains no direct syscall, thread-register
or reserved-register instructions. All four startup ELF images link and pass
their instruction and layout checks locally. At `a3e46d6`, all 30 added cases
pass in both signed Mac allocator modes and against native Linux libc. Both
fixed M2 scores remain 328/328. [Host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35976603927)
and [iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35976603997)
are green, including both full Linux ART startup profiles.

Downloaded evidence verifies the client/helper hashes, four ELF layouts,
eight signed framework layouts and 56 notice hashes. The integrated IPA also
verifies: seven iOS 15 ARM64 images, four embedded ELF payloads and 28 notice
hashes; both recorded allocator modes include the 30-case result. The IPA is
882,877 bytes, SHA-256
`e84f8202ec80a8b73df2db841bca5b4f8ad1489938a240054ff3a2bd4cc0861c`.
It still contains the native diagnostics, not ART.

In this Mac run the complete startup/client sequence takes 21.305 ms normally
and 33.811 ms with forced sampling; whole-process peak RSS is 6.52/6.19 MiB.
These are single correctness runs of the combined suite. The new libc checks
are not timed separately, and these measurements do not predict iPhone speed.

The first signed run caught a missing `__netf2` in the separate client: its
Android binary128 comparisons need a local compiler-rt helper, while Bionic's
copy is hidden. Link the already reviewed, hash-checked NDK comparison member
into that client as well. The client now requires all imports to resolve at
link time, which reproduces the original failure locally before packaging.
