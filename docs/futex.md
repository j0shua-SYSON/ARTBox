# Futex and exit-TID boundary

The initial process-local futex domain implements Linux WAIT, WAKE, WAIT_BITSET
and WAKE_BITSET, including the PRIVATE flag, plus plain REQUEUE_PRIVATE. WAIT uses a relative monotonic
timeout; WAIT_BITSET uses an absolute monotonic or realtime deadline. Timeout
bytes use Linux ARM64's two signed 64-bit fields. Invalid timeout data is checked
before a supported wait compares its word. Unsupported operations return ENOSYS.

One mutex serializes the atomic value comparison and waiter registration against
wake operations. Each waiter owns a native condition variable. The mapper locks
only during the aligned atomic access, so a blocked futex does not hold the VM
lock. Spurious native notifications are rechecked. Timed waits use the runtime's
actual clock provider, without assuming a C++ clock epoch; realtime deadlines are
rechecked at least every 100 ms. No executable memory or generated trampolines are
involved. Queue scan and native condition-variable overhead are not benchmarked.

Private and shared opcode keys are distinct. Shared operations currently support
the same address in one guest process; shared file aliases and multiple processes
are not implemented. A private wake does not need a currently mapped word.
Read-only words support atomic loads. The native thread reaper calls the
release-store/clear-TID helper after native join confirms the child stopped using
its stack and TCB; the helper then wakes one shared waiter. Real Bionic pthread
join/exit now passes in the signed six-worker fixture at `e828dad`.

The behavior reference is Linux v6.12's [syscall dispatch](https://github.com/torvalds/linux/blob/v6.12/kernel/futex/syscalls.c)
and [wait/wake implementation](https://github.com/torvalds/linux/blob/v6.12/kernel/futex/waitwake.c).
Those GPL sources are read as ABI references; no Linux implementation code is
copied or shipped. The original futex syscall has a legacy zero/negative wake
count quirk: it can wake one waiter. The tests compare this with the running Linux
kernel instead of imposing the newer strict futex2 behavior.

Native atomic access is a small platform interface: GCC/Clang atomic builtins,
or aligned 32-bit MSVC loads/stores in explicit `/volatile:ms` mode. The latter's
ordering follows Microsoft's [synchronization contract](https://learn.microsoft.com/en-us/windows/win32/sync/synchronization-and-multiprocessor-issues).
The portable mapper validates alignment and page access while holding its lock.
The read path does not use a read-modify-write operation, so it works on read-only
pages. The portable test includes publication through an acquire/release pair.

Local tests cover timed waits, mismatch and bad-pointer errors, masks, private
versus shared keys, active-domain destruction, clear-TID wakeup and 512 races
between enqueue and wake. The 19-case original C caller is built once with the NDK,
linked to real Bionic in the signed startup client, and reused with original and
adapted Bionic syscall objects on native Linux. All 19 cases pass in both signed
startup modes and both Linux profiles at `7b62337`; all 15 portable contracts and
the iOS regression build pass. [Host/Linux evidence](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34815207011)
and [iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34815209434)
are hash-paired with the downloaded caller and signed containers. That checkpoint
does not include the later interruption or requeue extensions below.

The configured non-restarting realtime interruption can terminate an ordinary
futex wait with EINTR. It uses the owning thread's delivery epoch and at most
5 ms polling intervals; see [the signal contract](m3-signals.md). Requeue retains
that owner, so moving a waiter does not detach its interruption state.

Plain FUTEX_REQUEUE_PRIVATE (opcode 131) takes signed 32-bit wake and move counts;
the fourth syscall argument is a count rather than a timeout pointer. It wakes
up to the requested number, then changes the key of up to the remaining move
budget, returning the combined count. Zero wakes really wakes zero. Both private
keys require alignment and a valid user address range but need not be mapped;
neither guest word is read or changed. Same-key movement counts each waiter once.
The source and destination keys share the existing queue lock, preserving atomic
registration versus wake/requeue, bitsets, deadlines and private/shared isolation.
Scanning is linear in the bounded active queue and needs no additional allocation.
The behavior reference is [Linux v6.12 requeue.c](https://github.com/torvalds/linux/blob/v6.12/kernel/futex/requeue.c);
the implementation and tests are original MIT code.

The separate 18-case NDK requeue caller covers counts, keys, error precedence and
word preservation. Its object is required in both signed Bionic modes and both
native Linux syscall profiles, without changing the original 19-case baseline
or M2's 328-case score. Portable tests additionally move actual blocked waiters,
select them by bitset at the destination, retain a timeout, interrupt a moved
waiter and exercise 128 insertion/requeue/wake races. Linux builds repeat the
queue tests against the actual kernel. All 34 local CTests pass; native CI for
this extension is pending. ART's unchanged condition-variable implementation
provides a further integration test during required JavaVM startup.

Shared requeue, CMP_REQUEUE, PI, robust owner death, wake-op and general restart
semantics remain unsupported; they are not silently treated as successful.
