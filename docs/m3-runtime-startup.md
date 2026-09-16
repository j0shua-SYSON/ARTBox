# Native ART startup reference

`scripts/test_art_startup.py` consumes the complete native Linux runtime, ICU,
libcore and implementation class-library builds. It verifies their source and
binary hashes and requires the same checkout revision. The script has configurable
input/output directories; execution requires a native ARM64 Linux host.

The latest [managed acceptance run at `7f9d8da`](m3-managed-checks.md) extends
the first hello result below with verified collection, explicit exceptions,
native-thread attachment and VM shutdown. Both host and iOS build workflows pass;
actual ART execution remains native Linux only.

## Verified native hello at 7d0ed24

[Host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35072533465) and
[iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35072533366)
pass. Original ART completes imageless bootstrap, invokes the original hello DEX
through the switch interpreter and exits zero after VM destruction. Its output is:

```text
ARTBox: ART started in 60.884 ms; switch interpreter, no JIT, no profiling cache
hello from ARTBox ART
ARTBox: real ART method returned the expected string
```

The complete process takes 115.745 ms. These are single-run correctness timings,
not steady-state interpreter performance. The 448-byte hello DEX has SHA-256
`6d080e7780d6e0c1187bddf33c12dd22b6b59fc630a48eb62f639971b75f1258`.
All eleven code-generation guard cases pass before startup, and the guard stays
active throughout the actual VM invocation. Runtime policy assertions pass before
and after the method. Both compiler stack-initialization controls also pass.

The downloaded startup archive has SHA-256
`4080ab464bb53e13e5b3760afed05e96702280d69c79e5fef308ac1b084855c8`.
Its 16 retained payload hashes verify, including eleven shared libraries, the
harness, all three DEX files and native corresponding source. Its twelve ELF
binaries are ARM64 with no writable executable load segment or executable stack.
The before/after process maps have no writable executable mapping, and all 18
executable mappings are unchanged. After the
method, 121 mappings total 934,653,952 bytes of virtual address space; resident
memory has not been measured. The build archive has SHA-256
`0f39f473921dcc258e810bfe8cacb58f19835ee89a4bfa72d1cc54da2cd8d696`;
all 458 object hashes, 1,425 selected upstream source files, 67 canonical project
files, 14 generated files and 20 notices verify.

The log retains expected missing-image fallback messages, incomplete time-zone
data and an attached-thread warning during VM destruction. Successful hello
execution does not establish those paths, explicit collection, attachment
lifecycle or Apple runtime support. The iOS build still contains the earlier
diagnostic fixtures; it does not yet execute ART. M3 remains incomplete.

## Invocation and policy

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

The extended harness adds [managed acceptance checks](m3-managed-checks.md)
using a separately built, verified DEX. It retains the original hello result as
an independent observation, then requires collection, exceptions, native-thread
attachment and VM shutdown to pass. Those extended checks pass on native Linux
at `7f9d8da`; the historical `7d0ed24` checkpoint above covers hello only.

## Bootstrap diagnosis

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
Restoring AOSP's automatic-storage zeroing policy allows the original VM and
hello method to run at `7d0ed24`; see ADR 0045.
