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

`artbox_vm_reserve_window` also attaches a retained window to an existing M2
syscall address space. `artbox_vm_mmap_window` allocates anonymous pages only
inside the selected window. Ordinary non-fixed mmap, file mappings and borrowed
image/stack ranges continue to use the rest of the address space. Both kinds of
allocation share the same lock, byte/region limits, page permissions, validated
I/O and mutation-failure state. Syscall mprotect, munmap and fixed replacement
recognize the managed pages and enforce each window's guard. Fixed allocations
through the explicit window API cannot target a different owned reservation.

Never register the whole managed reservation as borrowed storage: that would
grant syscall access to guards and holes. The mapper rejects overlapping borrowed
ranges. Unmapping the last managed page retains its reservation; ordinary owned
regions still release normally. This prevents Scudo's ordinary mmap demand from
consuming the managed window. The standalone convenience mode remains restricted
to one anonymous window, with file and borrowed mappings unsupported.

The [full high-heap profile](m3-high-heap-runtime.md) connects ART's MemMap and
card table to the window; its execution remains pending. Initial imageless
startup uses anonymous heap storage; actual runtime demand must determine any
additional mapping support.

The `stable_managed_window` test exercises native allocation, guard exclusion,
overlapping hints, fragmentation, zeroed reuse, protection, fixed replacement,
four allocating threads, failure after a native mutation, retained ownership
after all submaps are freed, and final release. It also reserves the full 4 GiB
range and reads/writes its last eight bytes through the reference codec while
committing only the final page. Unsupported file and borrowed-range paths must
not call a backing provider.

At `3ad7f50`, [full host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35951272480)
and [iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35951272471)
pass. The standalone-window test passes on Mac, Linux and Windows. The complete
portable suites pass 25 tests on Mac and 24 on Linux/Windows.

The additional `shared_managed_window` contract checks real syscall clock
copyout into active heap pages, guard/hole rejection without invoking an I/O
provider, protection and discard, two-window isolation, shared quotas, failed
reservation output preservation, mutation-failure isolation and final cleanup.
At `a700398`, [full host CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35952536999)
and [iOS build CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/35952536991)
pass. The shared-window test passes on all three hosts; complete portable suites
pass 26 tests on Mac and 25 on Linux/Windows.
These tests do not execute ART in a high heap or establish an iPhone VM budget.
The mapper stores one metadata byte per native page and searches holes linearly.
Allocation cost and a practical device reservation size remain to be measured
with the integrated runtime.
