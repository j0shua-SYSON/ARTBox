# Stable storage for managed references

`artbox_vm_create_window` gives the existing portable VM mapper one owned native
reservation, up to 4 GiB, and returns its checked reference-window descriptor.
It keeps the base fixed until destruction. A leading page-aligned guard remains
inaccessible and cannot be replaced by a fixed mapping.

Anonymous allocation honors a free, aligned hint inside the window or searches
for a contiguous hole. Unmapping discards the contents and makes the pages
inaccessible while retaining the reservation. Reuse therefore cannot let a host
allocation occupy a hole and later be overwritten by ART. Fixed replacement,
protection, reads/writes, discarding and locking use the existing VM operations.
Executable mappings remain rejected. A native mutation failure poisons the
mapper so stale metadata cannot authorize later access.

The ordinary VM mode remains the M2 syscall address space. The managed-window
mode currently accepts anonymous storage only; file mappings and borrowed
ranges return unsupported. This is intended for initial imageless interpreter
startup. ART's MemMap adapter and card table still need to use the window, and
actual runtime demand must determine any additional mapping support.

The `stable_managed_window` test exercises native allocation, guard exclusion,
overlapping hints, fragmentation, zeroed reuse, protection, fixed replacement,
four allocating threads, failure after a native mutation, retained ownership
after all submaps are freed, and final release. It also reserves the full 4 GiB
range and reads/writes its last eight bytes through the reference codec while
committing only the final page. Unsupported file and borrowed-range paths must
not call a backing provider.

Local portable tests pass; native Mac/Linux CI validation is pending. This does
not execute ART in a high heap or establish an iPhone virtual-memory budget.
The mapper stores one metadata byte per native page and searches holes linearly.
Allocation cost and a practical device reservation size remain to be measured
with the integrated runtime.
