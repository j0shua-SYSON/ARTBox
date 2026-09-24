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
their instruction and layout checks locally. Signed Mac execution and the new
native Linux comparison remain pending.
