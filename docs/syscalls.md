# Linux syscall compatibility

The five M1 contracts are implemented in portable C and pass Windows, macOS
and Linux CI. Both signed packaging candidates execute on native ARM64 macOS;
the identical original ELF runs on native ARM64 Linux. Physical iPhone execution
remains unverified, with physical-device gates waived for all milestones.

| Syscall (AArch64 number) | Implemented subset | Deliberate differences / missing behavior |
| --- | --- | --- |
| `write` (64) | stdout/stderr callback, byte count, readable-range validation, negative Linux errno | No file descriptors beyond 1/2, blocking I/O or signal interruption yet. |
| `exit` (93) | Low eight status bits; unwind to host invocation without ending the app | No guest threads/processes or thread-group lifecycle yet. |
| `mmap` (222) | Anonymous/private, chosen address, non-executable memory, page rounding | Requires flags `0x22`, fd -1, offset/address zero; 16 mappings and 64 MiB per context. |
| `mprotect` (226) | Whole owned mappings: none, read, read/write | Zero length, partial/unowned ranges, write-only and executable protection are unsupported. Linux permits cases outside this subset. |
| `munmap` (215) | Whole owned mappings and alignment validation | Partial ranges and holes are unsupported; Linux can unmap ranges containing holes. |

`tests/test_syscalls.c` checks return values, readable ranges, protection changes,
mapping cleanup, unsupported flags and exit unwinding. On Linux it also invokes
real host syscalls for mmap/write error cases, protection and unmap alignment,
memory access, and low-eight-bit exit status. Those checks are distinct from
the no-exec/ownership policy.

## M2 memory manager

`artbox_vm_syscall` routes the four memory numbers to a mutex-protected portable
address space. It is connected to the signed Bionic slice's NDK memory caller;
the M1 invocation keeps its original five-syscall context. Complete Bionic
startup and the M2 threads/files/mmap acceptance suite are still pending.

| Number | M2 subset | Limits / differences |
| --- | --- | --- |
| `mmap` (222) | Anonymous/private, page rounding, address hints, `MAP_NORESERVE`, fixed replacement within an owned reservation, demand-zero memory | Hints may be ignored; fixed mapping outside one owned reservation returns ENOTSUP. File/shared mappings are unsupported. Anonymous fd and aligned offset are ignored as on Linux. |
| `mprotect` (226) | Partial mapped ranges, none/read/read-write, write implies read for the Linux ARM64 target, zero length | Range must fit one reservation or registered image-data range. Executable protection returns EPERM. A hole returns ENOMEM. |
| `munmap` (215) | Partial ranges, holes, repeated unmaps and ranges spanning owned reservations | A partial unmap retains inaccessible host VA until the reservation's last live page is removed. Registered image/stack storage cannot be unmapped. |
| `madvise` (233) | `MADV_NORMAL` and anonymous `MADV_DONTNEED`, including read-only pages; discarded bytes are immediately demand-zero | Other advice returns ENOTSUP; holes/cross-reservation ranges return ENOMEM. Registered borrowed storage cannot be discarded. |

Native callbacks reserve inaccessible storage, change protection, replace a
subrange with fresh anonymous pages, and release reservations. The owner chooses
the VA budget and region count. Verified non-executable image data or host stack
pages can be registered as borrowed storage; the owner retains their lifetime.
No operation grants execute permission or replaces borrowed storage. A failed
native mutation stops further use of the address space because the host may
have changed some pages already; destruction still attempts cleanup.

Windows, macOS and Linux tests pass 1,024 concurrent mapping lifecycles, reservation limit
recovery, borrowed-storage ownership and injected failures, plus three actual
hardware protection faults caught in child processes. The same 35-case memory
caller is built by the NDK and linked into the signed Bionic wrapper; CI runs
that identical object through original/adapted Bionic on native Linux ARM64 and
through the translated syscall binding on macOS. All 35 cases pass at `2415969`.
These results are reported
separately from full allocator startup. See [ADR 0016](DECISIONS.md#0016---own-anonymous-reservations-and-track-page-state).

The Linux error values are explicit: EPERM 1, EIO 5, EBADF 9, ENOMEM 12,
EACCES 13, EFAULT 14, EINVAL 22, ENOSYS 38, ENOTSUP 95. Guest flags and host
`errno` are translated at the platform boundary. An unsupported syscall returns
ENOSYS; it is never forwarded to Darwin using the Linux syscall number.

The native page size comes from the host (supported contract: power of two,
4 KiB through 64 KiB). The fixture requests 16 KiB and both packaging routes
use 16 KiB Mach-O alignment. Memory allocated for the guest never requests
execute permission. File-backed mappings, virtual filesystem, futexes, guest threads,
signals, epoll, eventfd, pipes and sockets remain future work.

The M2 source profile routes all 216 generated Bionic syscall entries, 13 aliases,
the generic entry and inline queued signals through
`artbox_bionic_syscall(number, a0, a1, a2, a3, a4, a5)`. All arguments are
`uint64_t`; its `int64_t` result is raw Linux success/negative errno. Bionic's
original assembly tail converts errors to -1 and guest errno.

The host endpoint selects a borrowed immutable dispatch binding in native
thread-local storage. Returning calls preserve host errno. Unbound threads or
bindings without a handler return -ENOSYS. The owner keeps the binding/context
alive and restores the previous binding after nested entry or a non-local exit.
Guest TLS binding is separate. `native_syscall_boundary` checks 12 threads,
1,000 nested dispatches each, full-width arguments, errno and TLS isolation,
and the existing five-syscall translator through this exact exported entry.

Native Linux ARM64 also executes the actual NDK-built syscall entries and Bionic
errno helper: 8,280 capture cases and five real syscall smoke cases per profile.
The signal comparison uses the actual original/adapted header with a Linux test
backend. Neither test implements Darwin signals or counts toward M2's dynamic
runtime suite. Transporting a syscall number does not implement that syscall.
