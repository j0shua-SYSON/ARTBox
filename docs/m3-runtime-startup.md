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

At `9d89158`, the [native startup run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34967912962)
passes all eleven filter cases and calls original `JNI_CreateJavaVM`. Imageless
bootstrap aborts on `java.lang.String`: the reserved class occupies 824 bytes
and the linked class 792 bytes. VM creation does not return and no hello method
executes. The 1.368-second failed process duration is not interpreter performance.
The downloaded attempt retains matching library, DEX and corresponding-source
hashes, plus its log and memory map before VM entry.

The runtime now selects upstream's D8 desugaring configuration to match the
class-library compiler. ARM64 compilation of the original class-size expression
shows that this changes the reservation to 808 bytes, so it does not by itself
explain the entire observed mismatch. The host overlay adds the actual embedded
vtable length and static field offsets to the existing fatal diagnostic. It
does not change the comparison, class-size constants or loaded DEX.

The [diagnostic run at 67f0219](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35071200764)
confirms a 120-byte header, 808-byte reservation and 792-byte linked class with
79 embedded vtable slots. The three reference statics begin at 768, the bytes
occupy 780-782 and the long begins at 784. The expected 81 slots follow from
String's 73 declared virtual methods, Object's eleven and three overrides.
The next build restores AOSP's automatic-storage zeroing policy; see ADR 0045.
