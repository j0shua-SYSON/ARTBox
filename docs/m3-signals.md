# M3 signal boundary

Portable queues now implement guest thread-directed blocked standard signals
and synchronous waits. They do not install host handlers or change host masks.
Unblocked/default delivery and realtime queues remain unsupported; signal
registration and alternate-stack calls still return ENOSYS.

`tgkill` (131) resolves a guest PID/TID, supports the zero-signal existence probe,
and queues standard signals that the target blocks or is currently waiting for.
`rt_sigtimedwait` (137) consumes the lowest selected standard signal, with
coalescing, Linux SI_TKILL sender information, zero/finite/infinite waits and
Linux pointer/timeout ordering. A failed siginfo copyout still consumes the
signal. `rt_sigprocmask` (135) retains the existing mask and copyout rules;
unblocking a pending signal returns ENOTSUP without changing state until a
handler/default-delivery path exists. No host process is signalled.

Clone inherits the mask but no pending signals. Each guest thread owns a queue
until its actual native thread has joined; the reaper removes its TID before
waking Bionic's clear-TID waiter. Failed native creation rolls back queue
registration. A process with attached threads cannot be destroyed. Queue
operations use a process mutex and per-thread condition variables, so they are
for ordinary thread context only; a future asynchronous handler needs a separate
signal-safe path.

The shared 33-case C caller covers coalescing, selection order, sender layout,
invalid requests, zero-signal probes and copyout consumption. Local portable
tests pass it and exercise a blocked worker through the thread manager,
inherited masks, pending-set isolation, failed creation, finite timeout and
exited TIDs. CI runs the identical NDK object through signed Bionic and native
Linux. At `9c6a22f`, both signed modes and the identical NDK object on native
Linux pass all 33 cases, with independently checked source/object hashes and
logs. Both complete workflows pass; M2's fixed score remains 328/328.

The next context boundary uses a 4,560-byte Linux ARM64 ucontext, including the
31 general registers, PC/SP/PSTATE, FPSIMD and ESR records. The portable codec
uses caller-provided storage without allocation, locking or syscalls. Resume
extraction preserves the interrupted x18 and non-NZCV PSTATE bits, filters
unmaskable signals and rejects malformed or unsupported extension layouts
without changing outputs. SVE/SME/extra records are deliberately unsupported.
This is data conversion; the platform bridge must still validate guest code
and stack ranges, translate host state and install actual handlers.

Portable tests cover unaligned storage, exact wire fields, handler edits,
reserved-register preservation and malformed frames. A native ARM64 Linux
test compares the codec with real Linux/Bionic declarations, delivers a BRK
signal and resumes edited PC, x0 and SIMD v0 state while preserving x18. Its
negative control drops register edits and must fail. At `c687998`, this native
Linux test passes and detects dropped edits. Downloaded source/binary hashes
and the CTest log verify the actual kernel return; NDK header/layout compilation
and all 27 local portable tests also pass.

The Apple architecture adapter captures general/NEON/exception state through
SDK mcontext fields and thread-state accessors. Applying edits independently
preserves live host x18, non-NZCV CPSR bits and exception information. It does
not translate Darwin signal numbers, masks or siginfo, install a disposition,
or invoke an Android handler yet. The build targets ordinary arm64; arm64e
authenticated return addresses need a separate contract. A Darwin BRK test
and dropped-edit control are prepared for Mac CI, with signature verification
and retained diagnostics. Apple compilation and execution are still pending.

The pinned ART sources require a real boundary before JavaVM startup on Apple:

- `sigchainlib/sigchain.cc` installs SA_SIGINFO, SA_ONSTACK and SA_RESTART,
  reads back supported flags, and changes masks from inside the handler.
- `runtime/thread_linux.cc` registers, queries and disables a per-thread alternate stack.
- `runtime/signal_catcher.cc` waits for blocked SIGQUIT/SIGUSR1. Shutdown sends
  a thread-targeted SIGQUIT and joins the catcher.

The Linux reference in `fixtures/kernel-signals/linux.c` tests the raw ARM64
kernel layouts, mask filtering, invalid IDs/addresses, and copyout failures
after state mutation. It measures the kernel's alternate-stack threshold rather
than deriving it from a different libc's MINSIGSTKSZ. It also records accepted
action flags, actual handler masks, alternate-stack delivery and interrupted
PC/SP, standard-signal coalescing, sender information and a pthread's blocking
wait/wakeup. A second process suppresses the worker signal and must fail the
wakeup assertion. Handler observations use lock-free words and signal-safe
queries; the probe preserves errno.

Run `python scripts/test_signal_reference.py` on native ARM64 Linux. Build
commands, source/binary hashes, kernel release and both process results are
retained under the configured build directory. At `7921eff`, the native ARM64
Linux reference passes 70 checks on kernel 6.17.0-1022-azure and detects the
dropped-signal mutation. That kernel accepts action flags `0xdc000807` from
the all-bits probe and has a 5,120-byte minimum alternate stack. These are
measured Linux values, not Darwin constants or a portability guarantee.
Downloaded source/binary hashes and both process logs verify independently.
This reference does not run ART or prove Apple signal-handler translation.

The adapter must keep Linux encodings and thread IDs in the portable core,
with public host signal/pthread operations behind the platform interface.
Darwin signal numbers, siginfo and ucontext cannot be passed to Android code
unchanged. Preserve host handlers outside registered guest execution and
restore per-thread host state on detach. A handler must not enter the VM mapper
mutex, allocate, log through stdio, or consult mutable loader collections.
The existing syscall diagnostic path therefore cannot be reused directly from
a delivered guest handler. NoSigChain is not an accepted startup workaround.
