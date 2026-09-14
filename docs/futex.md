# Futex and exit-TID boundary

The initial process-local futex domain implements Linux WAIT, WAKE, WAIT_BITSET
and WAKE_BITSET, including the PRIVATE flag. WAIT uses a relative monotonic
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
Read-only words support atomic loads. A future native thread reaper must call the
release-store/clear-TID helper only after the child stops using its stack and TCB;
the helper then wakes one shared waiter. Thread creation/reaping is not claimed
by the primitive alone.

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
adapted Bionic syscall objects on native Linux. Its signed and Linux execution
awaits CI. Signals/EINTR, robust owner death, PI, requeue and wake-op are not yet
implemented; they are not silently treated as successful operations.
