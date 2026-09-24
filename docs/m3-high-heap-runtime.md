# Full ART with a managed heap window

The `--managed-window` runtime-build profile combines the pinned reference,
forwarding/JNI-storage and interpreter-argument changes with actual ART MemMap
and card-table integration. It builds all 463 runtime/support/test units, including
the portable mapper, native memory provider and original ARTBox bridge. The
existing 458-unit absolute-address reference remains a separate required build.

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

The changed AOSP units, fixture and bridge compile for ARM64 locally. At
`385a02d`, all 462 runtime/support units compile and libart links on native Linux;
the separately linked preflight fails because CardTable symbols are private to
libart. The helper now builds as the 463rd unit inside the test runtime library,
preserving AOSP's symbol visibility. Native execution remains pending.
Compile success does not establish startup,
GC correctness or performance. The expected next failures to investigate are
remaining raw-reference/native argument transitions, additional allocation
callers and shutdown lifetime assumptions. No iPhone is needed for this step.
