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
and dropped-edit control pass on native ARM64 Mac at `c16c648`. Independent
artifact checks verify six source hashes, the native binary, CTest results and
nine CodeDirectory page hashes, alongside the CI codesign verification. Both
complete CI workflows pass at that revision. This
proves Darwin context return through the codec; it does not call an Android
handler or establish physical iOS execution.

An Apple per-thread signal scope now provides the separate syscall dispatcher
and initialized guest TLS pointer needed during delivery. Its public pthread
key is created outside handlers and retained for the library lifetime; explicit
attach/detach owns per-thread storage. Signal-time lookup and scope swaps use no
allocation or locking. Ordinary compiler-TLS reads remain outside that path.
The new test faults while the VM mapper lock is held, then calls the adapted
Bionic endpoints through the signal scope; entering the ordinary dispatcher
would deadlock. Nested restoration, host errno, TLS-write rejection and thread
isolation are also checked. These native checks and both complete CI workflows
pass at `9200f91`. Independent inspection verifies sixteen project sources,
nineteen signed code pages and the positive/mutation outcomes. Disassembly
confirms that ordinary compiler-TLS resolution follows the unbound branch.

The initial delivery bridge now connects a real Bionic `sigaction` caller to
portable immutable action records and an Android-compiled SIGTRAP handler.
Registration reads/writes the 32-byte Linux ARM64 action, filters unmaskable bits
and publishes before old-action copyout, matching Linux error ordering. A
delivery owner must enable registration before guest threads start. Its validator
rejects unsupported flags/dispositions and checks signed code addresses.

The Apple bridge uses stable signed RX ranges and the attached guest stack;
its handler path never enters the VM registry or loader. It translates BRK to
Linux TRAP_BRKPT, enters a separate syscall/TLS scope and returns through Darwin
after checking guest PC/SP edits. Getpid/gettid and stack-buffer mask queries
are supported within this scope. The 16-check caller uses Bionic TLS/errno and
edits PC/x0/SIMD; a second call drops the register edits and must return -1000.
The same C source runs against native Linux libc, whose sigaction API layout
differs from Bionic; this is not an identical-object comparison. Both signed
Bionic modes and the native Linux comparison pass at `2aa064f`, including the
negative control. Independent source/object/binary and framework checks verify
the result; all 17 host CI jobs and the iOS build pass. The fixed 328-expectation
M2 denominator is unchanged.

This first transport accepts SA_SIGINFO with optional SA_RESTART, no action
mask, and SIG_DFL restoration. Default/fatal handling remains with the owner;
the diagnostic runner exits on unsupported faults. SA_NODEFER, SA_RESETHAND,
custom restorers and unsupported pending-signal delivery return errors. No ART
sigchain or JavaVM startup is claimed. Action
records remain allocated until process destruction, with explicit ENOMEM on
capacity exhaustion; the diagnostic owner allows 4,096 nontrivial publications.

The next extension adds `sigaltstack` and SA_ONSTACK. Portable per-thread records
hold the Linux stack address/size, published immutably for handler queries.
Registration requires owned RW guest storage, accepts Linux SS_ONSTACK input,
rejects active-stack updates and publishes before old-stack copyout. Clone with
shared VM starts disabled; Bionic then installs its own pthread alternate stack.
Records remain alive until thread detach, with explicit capacity exhaustion.

Each Apple execution thread owns a guarded host alternate stack for conversion.
The precompiled callback bridge places Linux siginfo/ucontext on the selected
guest stack and changes SP while invoking Android code. Guest SS_ONSTACK is
computed from guest execution, not the private host stack. A handler without
SA_ONSTACK stays on the interrupted stack, even when alternate storage exists.
The source test includes SP near the normal stack's lower bound, handler-time
query/error ordering, separate pthread storage and a missing-flag negative
control. At `e7e1647`, all 17 shared wire cases and 24 handler/worker checks pass
on signed Bionic and native Linux, with independently verified artifacts and
complete green CI. The extra worker has its own reported count, separate
from M2's six-worker acceptance group.

ARTBox advertises an 8 KiB AT_MINSIGSTKSZ for its frame/bridge budget; this is
larger than the 5,120-byte native Linux reference measurement. Each thread also
reserves at least 128 KiB plus two guard pages for host conversion. All pages
are data-only; no runtime code generation occurs. SS_AUTODISARM, nonlocal exits,
alternate-stack changes from a handler on the normal stack, and changed stack
metadata in a return context remain unsupported. No other fault transport or
ART startup is implied by this extension.

The extension verified at `e8408fd` publishes mask and pending bits in one coherent state. Apple
ARM64 requires lock-free 128-bit updates without outlined library calls; other
targets retain ordinary-context transitions and explicitly reject handler-time
mutation. Local tests cover 4,096 enqueue/unmask races, pending-unblock rejection
and mask calls while the VM mapper lock is held. A successful enqueue and an
unblock that strands it cannot both commit.

Delivery publishes the action mask and automatic self-blocking. Handler mask
syscalls now update shared state, while the ucontext retains the interrupted
mask. A validated return-mask edit is accepted if queued signals stay blocked.
The new native caller checks 18 assertions, including a cross-thread queue during
the handler and a subsequent wait after return. Omitting UNBLOCK fails as intended
on native Linux and both signed Bionic modes; all CI passes. Stable signed RO input and guest image RW output are
accepted alongside attached stacks; dynamic heap buffers are not yet supported
by the handler copy path. Restoring a mask that requires unblocked queued delivery
still fails explicitly, preserving the pending bits.

The next caller executes the unchanged pinned AOSP sigchain implementation. Its
registration probes SA_UNSUPPORTED and SA_EXPOSE_TAGBITS; the kernel facade now
clears probe flags outside the delivery owner's advertised 0x18000004 set.
Without a probe, unsupported flags still return ENOTSUP. No tagged-address
semantics are advertised. Action readback and copyout ordering have portable tests.
The guest caller uses named libart wrapper addresses to avoid libc interposition,
warms sigchain's real pthread key before delivery, then exercises special-handler
acceptance, fallback, removal, alternate-stack execution and scoped TLS mask
behavior. Twenty-two assertions and a removed-handler control are required on
both signed Mac and a same-source native Linux reference. Native execution of
this extension is pending; it does not start a JavaVM.
The reference compares each libc's actual policy: Bionic adds its reserved
POSIX-timer signal to sigprocmask, so the Android caller checks that bit alongside
the requested bits. The glibc caller expects no added bit. Masks are compared in
full, including the saved ucontext and restored state.

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
