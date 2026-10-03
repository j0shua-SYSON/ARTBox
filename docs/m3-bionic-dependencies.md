# Bionic dependencies for ART

The M3 Android ART link needs libc entry points outside the M2 startup subset.
Extend the same source-built Bionic from its pinned Android 15 revision.
`third_party/bionic/m2-objects.json` records the sources, their hashes and the
corresponding AOSP build flags. The selection now contains 338 units, including
its existing support components and the separately adapted vfork entry.

The latest native-class-library closure adds 57 unchanged units: Android
account lookup, numeric/DNS resolver routines, network/interface, file and
event wrappers, plus their BSD helpers. Numeric resolution does not contact a
DNS server. The account table comes from the pinned AOSP `aidarray` generator
and Android filesystem-ID header, not the host's passwd/group database or an
empty host table. Its upstream tests run unchanged on POSIX build hosts; the
generated output is normalized to LF and its hash checked on every platform.
Large object links use
response files so build paths do not exceed the process command-line limit.

A new client exercises 45 bounded string and numeric IPv4/IPv6 resolver cases,
plus 30 Android account/reentrant-buffer cases. The common cases have a native
Linux libc reference; Android's account IDs are checked only in real Bionic.
Short getnameinfo buffers retain Bionic's EAI_MEMORY and glibc's EAI_OVERFLOW
results. No runtime adaptation changes these original library semantics.
The new client fails strict linking against the previous source selection;
all 75 cases pass in both signed Mac modes at `1647514`, and native Linux
passes the 45 common cases. All 16 unchanged ID-generator tests also pass on Mac.
Its score is separate from
the existing 73 libc checks, 30 vfork checks and fixed M2 denominator.
The account-file tables and resolver pthread key add two original Bionic
constructors. Current acceptance requires all five constructors, while retaining
one mandatory startup expectation in the 328-case denominator. Earlier reports
with three constructors describe the earlier source selection.

The added closure includes BSD sorting/string routines, locale adapters,
path/directory and syscall wrappers, system-property writing, and time-zone
code. It adds no strong host imports beyond the existing Bionic boundary.
Building these functions does not add their missing kernel behavior: directory
enumeration, filesystem mutation, process creation, property-service sockets
and signal delivery still require their own implementations and acceptance.
The syscall compatibility matrix remains authoritative for those limits.

An original NDK client checks 73 sorting, basename/dirname, C-locale collation,
integer overflow/conversion, Android long-double conversion, wide-string,
multibyte, random-sequence, string concatenation, integer division, hostname,
environment, tokenization, numeric-address parsing and ancillary-header behaviors.
It runs inside the existing signed
Bionic process in both normal and forced GWP-ASan modes. The M2 suite's fixed
328-case denominator remains unchanged; this regression has its own required
result. Native Linux compiles the same client against system libc and compares
that result with both signed runs.

The iOS diagnostic package uses the same shared runner and libraries. Staging
requires the new 73-case result as well as the existing M2 acceptance. Compiler
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

The Android ICU link additionally requires strcat, strncat and div. The expanded
client fails to link against the old libc at those three symbols. Their unchanged
OpenBSD implementations are now selected at the existing Bionic pin. Twelve new
cases check returned pointers, terminators, untouched trailing bytes, bounded
nonterminated inputs, zero-length concatenation and signed quotient/remainder
behavior. The 274-unit native profile and startup client compile and pass local
instruction/layout checks. The new OpenBSD units retain AOSP's forced
`openbsd-compat.h` include, which supplies its DEF_STRONG compatibility macro.
At `4abb7b1`, the expanded 42-case suite passes in both signed Mac allocator
modes and against native Linux ARM64 libc. GCC's fortified inline strncat
diagnosed the intentionally bounded copy as truncation; calling the public
symbol through a volatile function pointer preserves the test and strict
warnings. Independent downloaded checks verify the caller, four ELF images,
eight framework layouts and 56 notices. Both M2 scores remain 328/328.

The native libcore link additionally requires gethostname, bsearch, strtok_r,
setenv/unsetenv, inet_pton and __cmsg_nxthdr. The expanded 73-case caller fails
against the previous library with precisely those seven missing symbols. Six
unchanged pinned source files provide them. Tests cover exact/short hostname
buffers, empty searches, independent token streams, environment replacement and
invalid names, IPv4/IPv6 parsing, and bounded ancillary-header traversal. Failed
gethostname output is intentionally unspecified inside its buffer, matching the
different Bionic/glibc behavior. No socket is opened by these tests.
Both 280-unit profiles compile locally with 1,552 shared global definitions;
the native instruction scan passes. The NetBSD bsearch source retains AOSP's
forced `netbsd-compat.h` include. At `030b310`, all 73 cases pass in both signed
Mac allocator modes and on native Linux ARM64. The complete
[host workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36300511614)
and [iOS workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36300511621)
pass. Independent downloaded checks verify the caller, four ELF images, eight
framework layouts, 56 notices and the Linux reference report. Both fixed M2
scores remain 328/328. These checks do not establish native libcore execution.
