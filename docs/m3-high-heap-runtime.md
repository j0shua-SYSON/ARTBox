# Full ART with a managed heap window

The `--managed-window` runtime-build profile combines the pinned reference,
forwarding/JNI-storage and interpreter-argument changes with actual ART MemMap
and card-table integration. It builds all 464 runtime/support/test units, including
the portable mapper, native memory provider and original ARTBox bridge. The
existing 459-unit absolute-address reference remains a separate required build.
Both include the direct [thread-state acceptance helper](m3-managed-checks.md)
inside libart; historical build counts below predate that additional unit.

Both profiles build their own native dependencies against the matching libart
and headers, then execute the same hello DEX and managed acceptance fixture on
native Linux ARM64. This isolates the managed representation and allocation
changes before the additional signed Apple/Bionic integration. It is not an
Apple ART execution result.

The harness binds one owned 4 GiB window before runtime startup and releases it
after all ART threads, heap spaces and runtime state are destroyed. Null
references remain zero before binding, so static null roots do not depend on
heap initialization. Non-null references use the checked byte-offset codec and
reject pointers outside the window. The initial variant uses the semispace
collector and plain references; existing focused native tests retain both
heap-poisoning profiles.

MemMap requests with `low_4gb` select the managed window before ART's absolute
32-bit address check. Direct fixed tail remapping and unmapping also route
through its owner. Protection changes update the same page registry. Ordinary
non-managed maps continue through the native backend in this Linux reference;
the Apple integration will use Bionic's shared syscall VM. The card table covers
the window's usable range. File-backed low-window images and mremap ownership
transfer are unsupported; requests fail instead of moving or replacing
untracked storage. Initial acceptance is imageless.

Before starting ART, an actual AOSP MemMap/CardTable fixture checks high
allocation, protection metadata, fixed tail replacement, retained holes, zeroed
reuse, ordinary maps outside the pool, rejected moving replacement, reference
round-tripping and dirty/clean cards at both ends of the heap range. Full
acceptance still requires the original managed graph to survive an observed GC,
expected exceptions, four thread-attachment cycles, detachment and destruction.
The executable-memory/process-execution denial filter remains active throughout.

The same preflight tests actual class-table slots for all eight low-bit hash
tags: checked encoding, decoding, raw encoded construction, copying, assignment,
empty entries, unchanged roots, moved roots and cleared roots. These tests use
aligned storage addresses without dereferencing class contents. The class table
stores checked heap offsets alongside its existing hash tags; its atomic root
update and descriptor comparison logic remain upstream implementations.

Large-object live/mark bitmaps also cover the complete managed window. Their
metadata includes the reserved guard, whose bits remain clear; allocation still
excludes that guard through the VM owner. A preflight uses the actual
DiscontinuousSpace constructor to check both extents, first/last page addresses,
out-of-window rejection, independent live/mark bits, copy and clearing. The
original bitmap storage and sweep algorithms retain their bounds checks.

The changed AOSP units, fixture and bridge compile for ARM64 locally. At
`385a02d`, all 462 runtime/support units compile and libart links on native Linux;
the separately linked preflight fails because CardTable symbols are private to
libart. The helper now builds as the 463rd unit inside the test runtime library,
preserving AOSP's symbol visibility. At `0979faf`, both full libraries build and
the managed-window MemMap/CardTable preflight passes on native Linux ARM64.
The process enters `JNI_CreateJavaVM`, then aborts in the checked encoder from
class-table lookup. The original class-table slot truncates native pointers and
reconstructs them as absolute 32-bit addresses. The original absolute-address
runtime passes in the same [CI run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35954934951).
This failure is retained in the startup artifact.

At `5acd098`, the class-table preflight passes, the high-heap runtime starts in
77.128 ms and the real hello DEX returns `hello from ARTBox ART`. The required
managed suite then aborts in `LargeObjectSpace::Sweep`: its live/mark bitmaps
still start at address zero and cover only the low 4 GiB. The original runtime
passes in the same [CI run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35956754203).
The bitmap regression and extent adaptation pass at `71f398e`, including the
complete managed GC, exception, thread and shutdown suite. Both full runtime
profiles and all other [host checks](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35958186945)
pass; the [iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35958186934)
also passes. This is native Linux evidence; the iOS app still contains earlier diagnostics.

Downloaded build evidence verifies 90 project inputs, 1,425 upstream files,
16 source adaptations, 463 compiled sources/objects and both linked binaries.
Startup evidence verifies 18 payload hashes, 103 canonical project files across
five source archives, all eleven code-generation denial cases and the failure
log. These are single-run correctness observations, not throughput measurements.

## Verified managed execution at 71f398e

Both runtime profiles execute the same DEX, preserve the rooted cyclic graph and
virtual-dispatch checksum 6496 across GC count 0 to 1, catch both expected
exceptions, complete four attachment cycles and destroy the VM with no remaining
registration. The high profile passes all three preflight contracts. Its window
starts at native address 280588879659008 with a 4096-byte guard. All eleven
code-generation denial cases pass, and all 18 executable mappings remain
unchanged through shutdown with no writable executable mapping.

| Observation | Original references | Managed window |
| --- | ---: | ---: |
| VM startup | 63.948 ms | 79.516 ms |
| Full diagnostic process | 116.086 ms | 116.107 ms |
| Peak process RSS | 27,808 KiB | 32,624 KiB |
| Observed managed allocation | 637,920 bytes | 637,920 bytes |
| Mapped virtual bytes after checks | 1,102,565,376 | 4,928,974,848 |
| Mapped virtual bytes after shutdown | 549,445,632 | 549,601,280 |

The high run reports a 1.458 ms explicit semispace collection, including a
1.418 ms pause. These are single correctness runs on separate Linux CI runners;
they do not isolate codec overhead, establish steady-state performance or prove
an iPhone memory budget.

The downloaded high-runtime build verifies 90 project inputs, 1,425 upstream
files, 17 adaptations and 463 compiled sources/objects. Both startup artifacts
verify their 18 payload hashes, five corresponding-source archives, canonical
project inputs, denial tests and complete mapping records. The high runtime
library SHA-256 is `c9aa28ef0e77029cb83dff0083079c80d0ea9692602a3c18eaf8c9b6e832c312`.
The next integration boundary is source-built Bionic TLS, signals and native
dependencies inside the existing signed Apple library packaging.

Compile success does not establish startup, GC correctness or performance.
The expected next failures to investigate are
remaining raw-reference/native argument transitions, additional allocation
callers and shutdown lifetime assumptions. No iPhone is needed for this step.
