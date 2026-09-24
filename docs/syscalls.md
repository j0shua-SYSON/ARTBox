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

`artbox_vm_read`/`write` now hold the mapping mutex across validation and copying,
so syscall buffers cannot be unmapped or protected concurrently by the VM.
Immutable signed code/constants can be registered for reads, with all protection,
replacement, discard and write operations rejected. Copying does not synchronize
guest data races, and the owner retains borrowed mappings until VM destruction.
Local tests race copies with 256 protection/unmap/remap cycles.

## Startup services

`artbox_kernel_call` adds per-thread system state above the M2 mapper. It uses
runtime-assigned positive process/thread IDs and native CSPRNG/clock callbacks;
the syscall translation and Linux byte layout remain in portable C.

| Number | Implemented subset | Limits / differences |
| --- | --- | --- |
| `getpid` (172), `gettid` (178) | IDs in the guest process namespace, common PID and distinct thread descriptors | The pthread bridge assigns distinct monotonic IDs to native guest workers. General process creation remains unsupported. |
| `set_tid_address` (96) | Store the exit-clear pointer without dereferencing it, return guest TID; NULL clears registration | The native reaper clears and wakes after join; detached Bionic exit disables clearing before deferred unmap. |
| `clock_gettime` (113) | Realtime and monotonic clocks; explicit little-endian 64-bit seconds/nanoseconds, including unaligned output | CPU, boot-time and dynamic clocks return EINVAL; coarse realtime/monotonic use the corresponding precise clocks. |
| `getrandom` (278) | Initialized host CSPRNG; valid Linux flags, zero length, EFAULT and partial progress on a later inaccessible range | No pre-initialization entropy state or guest signal interruption. Insecure requests receive secure bytes. Large transfers use 256-byte staging chunks and the Linux page-rounded signed-32-bit transfer cap. |

Windows uses BCryptGenRandom, the precise system clock and performance counter;
Darwin uses arc4random_buf and clock_gettime; Linux uses getrandom and clock_gettime.
No deterministic or user-space pseudo-random fallback is used. Linux flag rules
follow the [kernel implementation](https://github.com/torvalds/linux/blob/v6.6/drivers/char/random.c).
The local host contract checks buffer canaries, layouts, error propagation and
partial progress across an inaccessible page. Its Linux branch compares real
syscalls. An identical 36-case NDK caller is now linked into the signed wrapper
and both Linux Bionic paths; all 36 cases pass at `b17aaf2`, alongside all 12
portable CTest contracts on Windows, macOS and Linux.

The dispatcher also supports Linux REALTIME_COARSE (5) and MONOTONIC_COARSE (6)
using the corresponding precise native clock. The result has the same epoch and
ordering, with finer resolution and potentially more overhead than a Linux
cached coarse-clock read. Scudo requests this during actual startup.

The initial virtual descriptor table implements `/dev/null`, `/dev/zero` and
read-only `/dev/urandom`: faccessat, openat, read, write, lseek, fstat and close.
It provides independent guest descriptor numbers, reuse and exhaustion, serialized
concurrent operations, zero/null semantics and OS-random reads with partial
progress at an inaccessible page. Linux ARM64 stat bytes are encoded explicitly.
Absolute paths ignore dirfd; relative paths currently require AT_FDCWD with a
virtual cwd of `/`. Parent traversal is rejected. Unknown `/dev` names return
ENOENT; paths outside this initial device set remain ENOSYS. Regular files,
directory descriptors, dup/fcntl, devices beyond these three and entropy writes
remain unsupported. Native Linux checks compare the supported device behavior.

The Linux error values are explicit: EPERM 1, EIO 5, EBADF 9, ENOMEM 12,
EACCES 13, EFAULT 14, EINVAL 22, ENOSYS 38, ENOTSUP 95. Guest flags and host
`errno` are translated at the platform boundary. An unsupported syscall returns
ENOSYS; it is never forwarded to Darwin using the Linux syscall number.

The native page size comes from the host (supported contract: power of two,
4 KiB through 64 KiB). The fixture requests 16 KiB and both packaging routes
use 16 KiB Mach-O alignment. Memory allocated for the guest never requests
execute permission. File-backed mappings, the general virtual filesystem, broader
thread operations, signal delivery, epoll, eventfd, pipes and sockets remain future work. The initial
[futex implementation](futex.md) provides WAIT/WAKE and BITSET variants with
timeout, race and error tests; its 19-case signed Bionic/Linux comparison passes at `7b62337`.

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

The initial pthread clone bridge implements Bionic's exact CLONE_VM/FS/FILES/
SIGHAND/THREAD/SYSVSEM/SETTLS/PARENT_SETTID/CHILD_CLEARTID flag set. Original
Bionic code owns its TCB, stack, handshake and public pthread operations. Other
clone/fork/vfork forms remain unsupported. Native creation errors leave the parent
TID word unchanged; clear-TID and detached unmap occur only after native join.
The signed six-worker test passes at `e828dad` in both allocator sampling modes.

The rt_sigprocmask (135) implementation stores an independent guest
64-bit mask per thread and inherits it at clone. Size must be eight bytes; how
is checked only with a new mask. SIGKILL/SIGSTOP cannot be blocked. Input is read
before changing state, then the previous mask is copied out; EFAULT during that
copy does not roll back the change. Host masks are untouched. The 17-case original
NDK caller and pthread inheritance checks pass paired CI at `cc6b074`. This is mask state,
not signal delivery, alternate-stack or signal-handler support.

The [rooted VFS](files.md) now shares regular-file and virtual-device descriptors.
Its POSIX provider runs on macOS/Linux; the portable contract also runs with an
injected provider on Windows. Openat, read/write, lseek, fstat/newfstatat,
faccessat and close pass the supported flags/path/offset cases, including partial
I/O and 512 concurrent appends. `/system` is read-only and parent/symlink traversal
is rejected. Guest stat ownership is UID/GID 10000. File-backed data mappings and the 41-case NDK caller pass paired CI below;
mutable directories and multiple guest users remain unsupported.

### File-backed data mapping extension (native CI green at b1a94c5)

| ARM64 call | Implemented subset | Evidence |
| --- | --- | --- |
| mmap 222 | Private/shared regular-file data, independent FD lifetime, offset and permission checks; no executable or fixed file maps | 43-case identical NDK mapping caller; portable ownership tests |
| mprotect 226 | Preserve shared read-only descriptor ceiling across protection changes | NDK mapping caller and injected backing |
| munmap 215 | Partial file views, final reservation/reference cleanup | NDK mapping caller and injected backing |
| madvise 233 | DONTNEED restores private file pages and preserves shared changes; anonymous replacements remain zero-fill | NDK mapping caller |
| msync 227 | Shared MS_SYNC writeback, private/ASYNC no-op after validation; one reservation | NDK mapping caller |

`openat` flags use the ARM64 UAPI layout, checked at NDK compile time. The first
real Linux ARM64 file oracle exposed the initial generic-layout error; the
correction passes at `ce6c6eb` without removing any file tests.

### Initial proc snapshot

Openat/read/fstat/newfstatat/lseek/close now route `/proc/self/cmdline` through
an owned initial-argv snapshot, with zero inode size and independent offsets.
The 22-case original NDK caller passes Linux at `2125b46`; portable snapshot
ownership and fault tests pass. Both signed Bionic profiles and Linux comparisons
pass all 22 cases at `e50ec7f`, completing [M2 acceptance](acceptance/m2.md).
See [the proc scope](files.md#initial-process-command-line) for explicit limits.

### M3 retained managed windows

The runtime can attach an anonymous managed heap window to the existing VM
registry. Active pages participate in the same syscall buffer checks and memory
operations as ordinary mappings. mmap (222) with a fixed owned address cannot
replace a window guard; mprotect (226) rejects guards and holes; munmap (215)
retains managed reservation ownership even after its last page is freed.
Ordinary non-fixed mmap continues to allocate outside the pool. This retained-VA
policy deliberately differs from Linux, preventing other host allocations from
occupying future managed heap pages. The explicit window allocator is a host API,
not a new Linux syscall. See [the window contract](m3-heap-window.md) for validation
and remaining ART integration work.
