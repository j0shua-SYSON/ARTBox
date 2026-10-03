# Signed ART native bootstrap

At `d43ceaf44113e4e727364477bc5064a27db8ef8b`, the
[native Mac bootstrap job](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36294774467/job/108552449003)
executes the complete signed startup group on ARM64. This is a constructor and
pre-start JNI result; **JavaVM startup and DEX execution on Apple remain incomplete**.
The iOS framework is built for iOS 15; physical execution is unverified.
The complete [host workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36294774467)
and [iOS workflow](https://github.com/j0shua-SYSON/ARTBox/actions/runs/36294774469)
pass, including both original and high-heap Linux ART startup suites.

## Executed checks

The existing Bionic runner loads libc, ART, libm and libdl from signed frameworks,
checks their ELF resources, relocates writable data and initializes Bionic TLS.
The real guest libdl binding and shared VM, VFS, futex and thread services are
installed before constructors. All 31 constructors return, with one ELF TLS
module in the four-image group. Repeating group initialization runs no additional
constructors.

ART reports no registered JavaVMs and the pinned unsupported result for default
JNI arguments. Its heap bridge binds a 4 GiB reservation in the same mapper used
by Bionic, preserves the null-reference contract and unbinds. Loader, thread,
filesystem, futex and VM cleanup succeed. No JNI_CreateJavaVM call, DEX method,
managed allocation or collection occurs in this test.

| Single-run Mac observation | Result |
| --- | ---: |
| Load, data relocation and loader setup | 24.151 ms |
| Stack/thread setup, constructors, JNI/heap checks and join | 0.709 ms |
| Constructors returned | 31 |
| Linked images / ELF TLS modules | 4 / 1 |
| Registered JavaVMs | 0 |

Timings exclude final resource teardown and are diagnostic observations, not
interpreter benchmarks, full JavaVM startup times or iPhone measurements.

## Evidence

The producer is PR merge `a447f6aed2d4b635e68379a6276628dde340fa1a`, whose parent
is the implementation revision above. Downloaded artifact `10923921282`
(`art-guest-link`) has archive SHA-256
`dfb4fd070a5d9e67f65bff97d79abf837f455cd3744f11a49d168fe4b9f0a1c0`.
Its bootstrap report and captured stdout agree; the runner, 63 canonical project
source inputs, source archive, ELF and four input framework hashes verify.
The runner SHA-256 is
`b7d696deffff030ca1cd38b4ae6aaf5536dbeaeadd4472cb20375585319319cc`.

Independent full-link verification checks 176 canonical project inputs, the
nested original/adapted source archives, 16 injected capability-probe cases,
2,059,758 decoded instructions, both signed framework layouts and 44 notice
hashes. The 21,482,824-byte ART ELF has SHA-256
`9f92317e486776f18d7dc005144a5ab580dc4c43c7d9d19c8ee436a2674107af`.
Its packaged RX/RW spans are 11,255,808/278,528 bytes. Instruction checks find
no SVC, thread-pointer instructions or unknown instructions; the only three
x18 references are the previously reviewed context-snapshot stores.

The same revision's integrated diagnostic IPA verifies seven ARM64 iOS 15
Mach-O images, four ELF layouts, 28 notice hashes and empty entitlements. Its
894,272 bytes have SHA-256
`1385dce9e421c8e23b6a442e320f4dcb7af34bf3eb6a7fd3463b99072b15d579`.
It retains M1/M2 diagnostics and **does not embed ART**. Both M2 allocator modes
still pass 328/328 expectations; 14 TLS metadata rounds pass across seven threads.

## Remaining M3 risks

1. Signal registration, alternate stacks, masks, delivery and signal-wait wakeup
   must preserve Linux behavior without using mapper locks in a native handler.
   ART's catcher needs real wakeup and join behavior for shutdown.
2. Android native class libraries need signed packaging and loader integration;
   passing Linux reference builds does not make those artifacts Apple-loadable.
3. JavaVM creation must exercise the shared high heap and JNI boundary end to end.
   Variadic JNI calls must stay in Android-compiled code. Existing managed GC,
   exception, attach/detach, shutdown and no-JIT checks remain required.

See ADRs 0065–0067 for the bootstrap boundary, virtual uname identity and narrowly
adapted capability probe. Shared anonymous mappings, mremap and userfaultfd
remain unsupported.
