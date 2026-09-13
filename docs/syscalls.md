# Linux syscall compatibility

The five M1 contracts are implemented in portable C. Windows tests pass;
the M1 PR runs Linux oracle checks and Darwin/Windows implementations in CI.
Native guest execution and device results remain pending.

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
and memory access. Those checks are distinct from the no-exec/ownership policy.

The Linux error values are explicit: EPERM 1, EIO 5, EBADF 9, ENOMEM 12,
EACCES 13, EFAULT 14, EINVAL 22, ENOSYS 38, ENOTSUP 95. Guest flags and host
`errno` are translated at the platform boundary. An unsupported syscall returns
ENOSYS; it is never forwarded to Darwin using the Linux syscall number.

The native page size comes from the host (supported contract: power of two,
4 KiB through 64 KiB). The fixture requests 16 KiB and both packaging routes
use 16 KiB Mach-O alignment. Memory allocated for the guest never requests
execute permission. File-backed mappings, virtual filesystem, futexes, threads,
signals, epoll, eventfd, pipes and sockets remain future work.
