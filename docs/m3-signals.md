# M3 signal boundary

Guest signal delivery is not implemented. The current kernel adapter stores
each guest thread's logical Linux mask and inherits it during clone; it does
not install host handlers or change host masks. Signal registration and
alternate-stack calls still return ENOSYS.

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
retained under the configured build directory. The fixture compiles with the
NDK locally; native execution is pending CI. This reference does not run ART
or supply a guest implementation.

The adapter must keep Linux encodings and thread IDs in the portable core,
with public host signal/pthread operations behind the platform interface.
Darwin signal numbers, siginfo and ucontext cannot be passed to Android code
unchanged. Preserve host handlers outside registered guest execution and
restore per-thread host state on detach. A handler must not enter the VM mapper
mutex, allocate, log through stdio, or consult mutable loader collections.
The existing syscall diagnostic path therefore cannot be reused directly from
a delivered guest handler. NoSigChain is not an accepted startup workaround.
