# Native ART startup reference

`scripts/test_art_startup.py` consumes the complete native Linux runtime, ICU,
libcore and implementation class-library builds. It verifies their source and
binary hashes and requires the same checkout revision. The script has configurable
input/output directories; execution requires a native ARM64 Linux host.

The JNI harness preloads the real native libraries before installing a diagnostic
filter. It then enters original `JNI_CreateJavaVM` with switch interpretation,
JIT compilation and profiling disabled, no boot image and no runtime dex2oat.
Runtime assertions check the interpreter setting and absent JIT/code caches
before and after invoking the original `artbox.Hello.message` DEX method.

The Linux filter rejects executable `mmap`, `mprotect`, `pkey_mprotect` and
shared-memory attachment requests, process execution and personality changes.
Read-only personality queries remain usable. It synchronizes existing threads;
new threads inherit the filter. Eleven native tests exercise permitted data
memory and threads plus each rejected path. The signal diagnostic identifies the
system call and exits immediately. This is a test boundary, not a complete
security sandbox. Its argument checks and thread behavior follow the
[kernel seccomp documentation](https://www.kernel.org/doc/html/latest/userspace-api/seccomp_filter.html)
and [seccomp API](https://man7.org/linux/man-pages/man2/seccomp.2.html).

Each attempt preserves stdout, stderr, elapsed process time, input hashes, native
libraries, DEX, corresponding source and memory maps before/after successful
execution. Writable executable mappings fail the probe. A timeout, abort, failed
JNI lookup or wrong string fails CI. Build success and failed startup attempts
do not count as Java execution. Physical and signed Apple runtime acceptance
remain separate from this Linux reference.

The first probe establishes VM startup and one method return. Allocation,
exceptions, collection and thread attachment still require the additional
checks in [the M3 contract](m3-contract.md).
