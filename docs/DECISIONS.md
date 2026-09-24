# Architecture decisions

## ADR 0001 - Cross-platform build tooling (accepted, M0)

Use Python's standard library for build orchestration, validation, packaging and
environment setup. CMake builds the portable core and generates the Xcode app.
Windows, macOS and Linux share the same entry point. Build/cache paths are configurable; existing user SDK and credential settings
are inherited. Keep host-specific policy, scripts and notes in ignored `work/`.
See [building](building.md).

## ADR 0002 - Small C platform boundary (accepted, M0)

The portable core emits length-delimited messages through a synchronous
caller-owned callback. The host writes stdout; UIKit copies messages before
dispatching to the main queue. No global logger, platform headers or UIKit
objects enter the core.

## ADR 0003 - CMake generates the Xcode app (accepted, M0)

Use one CMake definition for portable C and a conditional UIKit app target.
Target arm64/iphoneos/iOS 15 or later. Build Release with signing disabled before
explicit ad-hoc transport signing. Generated projects give Mac users a normal
Xcode provisioning entry point. The bundle identifier is configurable.

## ADR 0004 - Record build and execution evidence separately (accepted, M0)

Host CI, iOS compile/sign verification, and physical-device execution establish
different things. An empty-entitlement `codesign -s -` IPA is a transport
artifact, following S5LBox's build pattern. Record each milestone's CI results,
measurements and device status before tagging. The project owner waived physical
device checks for all milestones. They are optional evidence, never a milestone
gate. Automated acceptance, CI and artifact checks remain required; a waiver
does not establish physical-device execution or lifecycle behavior.

## ADR 0005 - Signed native packaging (accepted, M1)

The pre-implementation [loader comparison](loader-design.md) required both
approaches against identical ELF input. Both signed candidates now execute on
ARM64 macOS and build for iOS 15; see [M1 evidence](acceptance/m1.md). Select the
Apple-linker wrapper for M2: Apple tools own container metadata, the signed
fixture is smaller, and the host experiment shows no invocation penalty.
Keep the converter as an experimental comparison. Physical iPhone execution
remains unverified under ADR 0004. Signing bytes does not adapt Android TLS,
reserved registers or the function ABI.
No writable/executable mappings or runtime instruction mutation. Unsupported
post-install native code must fail explicitly.

## ADR 0006 - Scope and license boundaries (accepted, M0)

M0 contains no AOSP. Inspect and pin only the components required for the current
milestone. Dependencies retain their actual licenses, including Bionic's BSD
notices. No GPL reference implementation is copied into the MIT core. No GMS,
vendor blobs, guest kernel, CPU interpreter or JIT is part of ARTBox.

## ADR 0007 - Python for build-time packaging (accepted, M1)

The packer is a Python standard-library host tool. It validates a controlled
static ELF, replaces syscall sites before signing, and emits both a direct
Mach-O dylib and an assembly wrapper containing identical transformed bytes.
The runtime parser, syscall dispatcher and memory interface remain portable C.
The host packer's stricter input validation is independent of the runtime parser;
neither is a general dynamic linker yet.

Keep code and read-only constants at their original relative addresses in one
Mach-O text segment. The wrapper uses a regular section in `__TEXT` because it
contains both instructions and constants. LLVM rejected `some_instructions` as
an assembly section attribute; segment protection, not that annotation, controls
execution. Local LLVM inspection validates the container/export layout and
generated veneers. ARM64 macOS CI additionally loads both signed frameworks
through dyld and executes their guest code on the native CPU. The iOS build
checks signatures and bundle contents; physical execution remains unverified.

The instruction allowlist enforces this fixture's ABI contract. It does not
establish isolation from malicious native code inside the host process.

## ADR 0008 - Pin Bionic independently of the platform tree (accepted, M2)

Use the AOSP Bionic `android-15.0.0_r1` archive and commit recorded in
`third_party/sources.json`. Fetch through the GitHub CLI and verify archive and
notice hashes. Stage original sources in the configured cache; compatibility
patches and build selections belong in the repository. Extract regular files
and materialize internal header aliases without requiring symlink privileges.
Omit the listed editor/versioner files and unused, case-colliding netfilter
headers. Additional allocator and loader dependencies require their own pins
and license reviews. The NDK supplies a compiler, not a substitute runtime libc.

## ADR 0009 - Preserve ELF load bias in the signed wrapper (M2 prototype)

Start with source-built Bionic and suite ELFs using a controlled link layout.
Group immutable content in signed Mach-O text and writable content in aligned
data segments. Verify that the final Apple-linked image preserves the ELF
address differences for every loaded range before signing. This permits normal
data relocations using one image load bias. Reject layouts that fail that
comparison; do not repair signed instructions at runtime.

An arbitrary existing ELF may need retained static relocations and further
build-time transformation. That generalization is not established by a
controlled source-built image. The runtime loader will validate dynamic tables,
resolve dependencies and symbols in a guest namespace, relocate owned writable
data, establish TLS, protect RELRO and invoke precompiled constructors. Each
operation needs tests before it is used by Bionic.

The first [dynamic prototype](dynamic-wrapper.md) uses the real Bionic syscall
slice, a controlled two-load link script and a constructor/data probe. Apple
linking must preserve both section byte hashes and their original ELF distance.
The wrapper materializes BSS as zero bytes and adds 16 KiB page padding; those
costs avoid runtime executable allocation or instruction fixups. Native Apple
execution and signing verification are required before accepting the prototype.

## 0010 — Prepare data relocations before modifying a loaded image

Status: accepted for M2's relocation API; runtime integration pending.

Signed code cannot be repaired at runtime, and a failed import lookup must not
leave a half-relocated data image available to guest code. The portable engine
therefore takes explicit writable PT_LOAD views and immutable source metadata.
It prepares all RELA/RELR writes, checks destinations and resolves ordinary
symbols before committing any bytes. It never maps memory or calls guest code.
PLT entries resolve eagerly, avoiding a runtime-generated lazy-binding path.

The cost is temporary storage and sorting proportional to relocation count.
Overlapping/composed writes are rejected; IFUNC and TLS need separate runtime
support before they can participate. A caller-provided resolver keeps Android
symbol scope separate from host `dlsym`. RELRO protection belongs after data
relocation, and the signed-package verifier must independently establish that
the supplied load bias matches executable and writable segment addresses.

Portable failure tests and byte comparisons against LLVM-expanded relocations
on actual NDK outputs cover this API. They do not establish Bionic execution.

## 0011 — Keep guest TLS in host TLS and preserve Apple's reserved registers

Status: accepted for the current Bionic source profile; dynamic execution pending.

The first object inventory showed that compiler register reservations alone do
not remove Bionic's inline x18 writes or TPIDR_EL0 access. Apple's [ARM64 ABI
documentation](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms)
reserves x18. Replacing Darwin's thread pointer is incompatible with host code
on the same native thread.

We considered a dedicated guest register, a host thread-key lookup, and the
host compiler's ordinary TLS. Use ordinary host TLS to hold a borrowed Bionic
slot pointer, with swap/restore for nested entries. Pointer-only precompiled
C endpoints provide Bionic's getter and setter; the hidden `__set_tls` wrapper
remains defined inside Bionic. The runtime must establish the guest TLS layout
and own its lifetime before calling Bionic. No generated trampoline is needed.

The source overlay omits Android shadow-call-stack setup and cleanup; packaged
guest code must not use that x18-based instrumentation. Compiler stack checks
remain enabled and use Bionic's global guard, preserving upstream exceptions
for bootstrap/initialization code. Baseline Armv8-A atomics are emitted inline
instead of importing Android's outlined, CPU-feature-dependent helpers. These
choices trade an extra TLS call/lookup, potentially slower atomics and loss of
Android SCS for an ABI that can coexist with the host. Timing and hardening
assessment remain required; the build does not claim equivalent protection.

Edits apply only to hash-verified source copies under the build directory.
The upstream control and adapted profile preserve 171 global definitions across
34 units. The native object has no checked TPIDR, x18/x27/x28, SVC or unknown
instructions. Host tests verify per-thread and nested binding isolation. These
checks do not prove full Bionic ABI compatibility or native guest execution.

## 0012 - Route inline signal delivery through the raw syscall boundary

Status: accepted for the Bionic source build; Darwin signal support pending.

Adding the real fd-ownership diagnostics exposed two inline `svc` instructions
in `fdsan_error`, through Bionic's `inline_raise` header. Keep the diagnostic
and fatal behavior; route the kernel entry through `artbox_bionic_syscall`
with one syscall number and six unsigned 64-bit arguments. The result is a
signed 64-bit raw Linux value. No variadic call crosses the Apple/Android ABI.
The source overlay retains the original implementation for the upstream control.

The endpoint must preserve guest errno, including on failure: the original
inline assembly discards the raw negative result without changing errno. An
ordinary libc `syscall` call would change that behavior. Arguments continue to
use Android signal numbers and the Android `siginfo_t` wire layout; the future
runtime must translate them, not forward them directly to Darwin. The endpoint
is still unresolved in the partial Bionic object. Nothing silently handles or
discards fatal signals on iOS.

The cost is an additional precompiled call in this diagnostic path, with no
runtime code generation. The Linux comparison builds the actual original and
adapted headers, exercises thread-targeted queued signals with non-null/default
payloads, and tests errno preservation on invalid signals. Its raw test backend
forwards to Linux; this does not establish Darwin signal support or Bionic startup.

## 0013 - Adapt AOSP-generated syscall assembly and retain Bionic errno conversion

Status: source build and native Linux oracle pass; complete runtime integration required.

Use AOSP's pinned table and generator for the exported ARM64 stubs and aliases.
A source adapter replaces each `svc` pair with a normal precompiled C call and
preserves FP/LR and unwind metadata. Up to six argument registers shift to make
room for the Linux syscall number; unused words are zero. The generic syscall
entry can forward its existing seven register arguments directly. Preserve the
original Bionic error-range test and tail call into `__set_errno_internal`.

This avoids handwritten duplicate syscall numbers/prototypes and any runtime
instruction patching. The cost is a stack frame, argument moves and a host call
per entry, not yet timed. Nonstandard clone and thread-stack teardown entry
points require separate implementations; ordinary table stubs cannot provide
those thread lifecycle semantics.

For verification, prefix a relocatable test copy so no Bionic export interposes
on the Linux host libc. Link the actual Bionic errno setter and provide separate
test TLS for its `__errno` dependency. Bulk cases use a capture backend with
controlled results, exercising every generated entry/alias and callee-saved
registers without issuing arbitrary Linux calls. Five explicit smoke cases run
against Linux in both profiles. These checks do not establish the full runtime.

## 0014 - Keep the AOSP allocators and bind their state before entry

Status: compiled and instruction checked; allocator runtime integration pending.

Use Scudo and GWP-ASan from the matching Android 15 tag, with their real Bionic
wrappers and complete notices. Retain Android's Scudo configuration and use its
existing switches to disable TBI/MTE in both build profiles. Pointer tagging and
hardware memory-tag diagnostics are not available in this configuration. Keep
the guarded allocator and ordinary Scudo checks; do not substitute a host malloc
or claim equivalent Android hardening. Scudo's ordinary Android mapping pattern
requires more than the M1 mapper supports and must be implemented/tested next.

GWP-ASan's initial-exec ELF TLS remains incompatible with host thread-pointer
ownership. Use its platform TLS header hook to reach a real upstream state
object through Bionic's native-bridge guest-state slot, which ARTBox owns. This
adds the existing Bionic TLS call/host lookup to sampling operations. The runtime
owns aligned storage, upstream constructor initialization and the slot binding
before entry. No allocation occurs in this hook. That initialization is not yet
connected to Bionic startup. The Linux comparison exercises actual NDK-built
state access on eight native threads in both profiles.

Replacing one ELF TLS object and adding calls changes weak C++ template outlining
at `-O3`. Record the exact five symbol differences and require them to remain weak;
all other global definitions must match. Also require exactly the original
eight-byte GWP TLS definition in the control and none in the native object. This
retains a precise build comparison without treating compiler-generated template
outlines as Android libc exports. Execution and performance remain separate checks.

## 0015 - Use AOSP's baseline ARM64 string dispatcher

Status: selected; native guarded-page checks are required in CI.

Use the unchanged Bionic `static_function_dispatch.S` and the 14 matching
Arm optimized routines from AOSP `android-15.0.0_r1`. They provide 15 public
memory/string entry points, including both memcpy and memmove. This is the
existing static baseline configuration, so these functions need no IFUNC
resolver or runtime instruction patch. Remove the temporary generic strrchr
provider to avoid duplicate definitions. Both source profiles use this selection.

Fetch only the required assembly, shared header, build description and complete
license, with exact file hashes. Preserve the Arm source notices and license in
object and signed-framework artifacts. The `*-mte` names describe granule-safe
algorithms using baseline ARMv8-A and Advanced SIMD; no memory-tagging feature
is enabled. Retain memset's DCZID_EL0 check: its 64-byte DC ZVA path runs only
when the CPU advertises that size and permits it. Other configurations take
the vector-store path. The native tests include large zero fills.

The cost is losing CPU-specific IFUNC selection and any resulting performance
benefit; there is no benchmark against Android's tuned dispatcher yet. The
same NDK test object runs 35,908 cases against an independent scalar oracle on
Linux and through the signed macOS wrapper. Buffers have guard pages on both
sides; cases include zero length at the boundary, unaligned starts, unsigned
comparison, copy footprints and overlap in both directions. The original test
caller deliberately has no stack-guard/libc dependency; production C/C++ stack
protection is unchanged. These tests do not establish complete Bionic startup.

## 0016 - Own anonymous reservations and track page state

Status: implemented; Windows, macOS, Linux and signed-wrapper native comparisons pass at `2415969`.

Scudo reserves inaccessible VA, replaces parts with fixed anonymous mappings,
trims subranges and discards pages expecting zeros. Implement these operations
in a portable C++ address-space manager with a C API, using small native reserve,
protect, reset and release callbacks. A mutex serializes mapping metadata and
native mutations. Memory accessibility checks are snapshots, not pins against
another thread unmapping storage. Guest accesses must stop before destruction.

Keep each host reservation until its last guest page is unmapped. Windows
[VirtualFree](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree)
can decommit subranges but releases a reservation as a whole. This choice gives
all hosts the same ownership model; the cost is retained inaccessible VA and
one metadata byte per native page. The caller sets reservation and region
budgets. Scudo's full VA usage and physical iPhone limits remain unmeasured.

For Linux `MADV_DONTNEED`, replace only owned anonymous pages at the same VA.
[Linux's contract](https://www.man7.org/linux/man-pages/man2/madvise.2.html)
requires zero-fill-on-demand for private anonymous memory; Apple's
[documented advice](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/madvise.2.html)
does not promise those zeros. Remapping incurs a mapping operation rather than
a paging hint. Windows uses decommit/recommit. Partial unmaps use the same reset
with no access. No path requests executable pages. Write-only guest protection
maps to read/write, matching the
[Linux ARM64 protection map](https://github.com/torvalds/linux/blob/v6.6/arch/arm64/mm/mmap.c)
and a paired native Linux test.

Permit protection changes on registered, verified non-executable image data
and host thread stacks, but reject replacing, discarding or freeing those borrowed
ranges. Fixed mappings may only replace one owned reservation; file/shared
mappings and protection/advice spanning separate reservations remain unsupported.
A failed native mutation invalidates subsequent address-space operations; stale
metadata must not authorize further access. Failed initial allocation rollback
remains tracked for destruction to retry.

Tests exercise allocator-shaped reservation/commit/trim sequences and immediate
zeroing, quota recovery, borrowed ownership, failure cleanup and concurrency.
Child processes catch expected hardware faults without crash/core artifacts.
An identical NDK caller tests the real Bionic error tails in the signed Mac slice
and both original/adapted Bionic Linux paths. This proves a memory boundary;
executing the actual allocator and constructing guest TLS remain separate work.

## 0017 - Keep binary128 arithmetic inside the Android ABI

Status: implemented; native Linux and signed macOS arithmetic tests pass at `726d758`.

Android ARM64 long double uses binary128. Apple's ARM64 long-double ABI differs,
so binding Bionic's compiler arithmetic helpers to host symbols would silently
change the operation and calling convention. Link exactly the two reviewed
compiler-rt members supplied by the pinned NDK for comparison and multiplication.
Their member hashes and complete supplied LLVM notice are checked in both source
profiles. This adds ordinary AOT runtime arithmetic, with no generated code and
no host floating-point bridge. Their 387 instructions pass the native boundary
check; software binary128 cost has not been benchmarked in Bionic workloads.

The test caller constructs independent integer encodings and checks exact product
bits, ordering, infinities, subnormals, rounding ties, signed zero and NaNs. It
stays in the Android ABI for helper calls and exposes only an integer result.
The same NDK object runs through the signed Mac wrapper and native Linux runner.
Keep test-only symbol prefixes separate from production definitions. These checks
do not cover all floating-point environment modes or general variadic ABI bridges.

## 0018 - Stage syscall copies through the owned address space

Status: implemented; portable and signed-native startup-service checks pass at `b17aaf2`.

Use the VM's mapping mutex for validation and copying as one operation. Snapshot
access checks alone cannot prevent another guest thread changing protection or
unmapping a buffer before a syscall touches it. Immutable signed input ranges
may be registered for reads, with mutation and protection changes forbidden.
The owner retains their lifetime. This guards mapping changes, not guest data
races or arbitrary native memory access.

Stage CSPRNG output in 256-byte host buffers, then copy into guest memory under
the lock. Report bytes already copied if a later chunk becomes inaccessible.
Encode Linux ARM64 clock results explicitly as two little-endian 64-bit words;
never expose host timespec layout or clock IDs. Use native OS randomness only:
BCryptGenRandom on Windows, arc4random_buf on Darwin, getrandom on Linux. The
host is already initialized; entropy-pool readiness and guest signal interruption
are not emulated. Additional staging and mutex operations cost time, not code
generation. Per-syscall overhead has not been benchmarked.

Thread descriptors carry guest PID/TID and the pointer registered by
set_tid_address. Registration stores that pointer without touching its memory,
as Linux does. The later thread-exit implementation must clear/wake it after the
native thread stops using its stack; this change does not claim that lifecycle.
Tests include invalid flags, unaligned clock outputs, read-only buffers, partial
random progress, provider failure and concurrent copy/unmap. The same NDK caller
checks the public Bionic error conversion on Linux and through the signed wrapper.

## 0019 - Supply virtual entropy devices for real libc startup

Status: implemented; portable tests and signed Bionic startup pass at `5671845`.

The first actual shared-libc startup reached final Bionic TLS and its priority-1
constructor. It aborted with `ran out of AT_RANDOM bytes, have 0, requested 1`.
The stack guard and setjmp cookie consumed the initial 16 bytes; GWP-ASan then
needed another random byte. Bionic chooses its early-boot fallback when access
to `/dev/urandom` fails, even when getrandom itself is implemented.

Provide virtual null, zero and urandom devices with an owned descriptor table.
This is already required by the Linux ABI layer. Keep the AOSP entropy code
unchanged. Alternatives were a source overlay that always uses host randomness,
or postponing optional allocator initialization; neither is needed for this
failure. A fake successful access check without a functioning device would not
satisfy the interface. Device reads use the existing OS CSPRNG and VM-locked
copies. Virtual urandom is explicitly read-only; entropy injection is unsupported.
No host paths or descriptor numbers are forwarded from the guest.

Scudo also requests CLOCK_MONOTONIC_COARSE. Translate both Linux coarse clocks
to the corresponding precise native clock. This can cost an extra clock lookup
versus Linux's cached coarse value; it avoids uninitialized timestamp data and
adds no runtime-generated code. Portable tests reproduce the missing-clock
failure before the fix and compare clock contracts with native Linux.

## 0020 - Serialize futex admission with native wait queues

Status: implemented; local and signed Bionic/Linux comparisons pass at `7b62337`.

Use a process-owned queue mutex and native condition variables. Holding the queue
mutex across the atomic word comparison and waiter insertion prevents missed
wakes; the VM mutex covers only the actual word access. Keep private/shared keys
distinct. Introduce precompiled atomic callbacks to avoid casting guest storage
to a host C++ atomic object's layout or performing a write during a read-only
futex load. Microsoft ordering is an explicit compiler option for its platform
source; Clang/GCC use their atomic intrinsics.

This is slower than an uncontended kernel-assisted fast wake and cannot yet
model signal interruption, shared aliases or priority inheritance. It does not
require an iOS entitlement or instruction patch. The same runtime clock provider
controls deadlines, including relative versus absolute timeout differences.
Keep the exit-clear operation separate from native thread ownership: clearing a
TID before its native child stops using the stack would permit a premature join
and unmap. See the tests and references in [futex semantics](futex.md).

## 0021 - Reap native threads before releasing Bionic stacks

Status: portable and signed Bionic pthread tests pass at `e828dad`.

Retain AOSP pthread allocation, handshake, join state, key destructors and teardown.
A small adapter compiled against the pinned private headers describes the usable
stack and TLS through fixed-width fields. The host supports Bionic's pthread clone
flag set; other clone forms remain ENOSYS. Public POSIX pthread attributes select
a supplied non-executable guest stack. Round its upper bound down to a native
page, leaving the guest TCB and up to one page outside the host's usable stack.
Windows has no matching public supplied-stack thread API and returns ENOTSUP;
portable lifecycle tests use an injected std::thread backend there.

A process-owned reaper joins each native worker before clear-TID/wake or detached
unmap. Publishing the parent TID holds the VM word lock across native creation:
a failed creation leaves the word unchanged and cannot return a still-running
worker to a caller that would free its stack. Test a delayed native tail after
the guest callback returns, creation failures, 130 reaps and actual POSIX stacks.
TIDs are monotonic within this bounded process; general clone/process creation,
scheduling, cancellation and robust mutex cleanup remain unimplemented.

Final guest thread exit crosses a C setjmp boundary after Bionic runs its own
cleanup. The native worker then returns normally so host thread-local teardown
finishes before the reaper proceeds. Using pthread_exit directly would require
host forced unwinding through Android frames that are not registered Apple
unwind metadata. The selected approach adds a host root frame and reaper queue;
it generates no code and requests no entitlement. Each guest root frame owns its
GWP-ASan state using the real AOSP definition and binds it to the guest TLS slot.

## 0022 - Keep guest signal masks in the thread descriptor

Status: local and expanded signed/Linux comparisons pass at `cc6b074`.

Store the Linux 64-bit blocked mask per guest thread and copy it at clone.
Implement rt_sigprocmask ordering, block/unblock/set, immutable KILL/STOP bits,
unaligned bytes and old/new aliasing. Mask changes survive a bad old-mask output
pointer, matching the [Linux syscall contract](https://github.com/torvalds/linux/blob/v6.12/kernel/signal.c).
The original NDK syscall caller adds 17 cases before this implementation and
fails on the missing syscall; it is reused by both Linux profiles and the signed
Mac slice. The real pthread client checks inheritance and child/parent isolation.

Do not translate guest masks to native pthread_sigmask. Host signal numbers and
host runtime requirements differ, and masking host faults would interfere with
native diagnostics. This bookkeeping is a prerequisite for future guest delivery,
not a successful delivery stub. sigaction, alternate signal stacks, pending
queues, interruptible futex waits and signal frames remain unimplemented. The
mask path uses no executable allocation or runtime code generation.

## 0023 - Share rooted file and device descriptors

Status: portable/native VFS and signed Bionic file integration pass at `ce6c6eb`.

Extend the portable descriptor table so devices and backing files cannot collide.
Use a borrowed preopened filesystem root and opaque file handles behind a small
interface. Resolve one component at a time; disallow symlinks and parent traversal
for the initial confined profile. This deliberately excludes symlink compatibility
until a bounded resolver can preserve the same root. POSIX uses public openat
and NOFOLLOW; the Windows provider remains ENOTSUP rather than presenting path
string checks as equivalent to descriptor-relative confinement.

Keep Linux stat encoding and flag/error policy in the core. Serialize file offsets
and hold VM access validation across I/O so errors do not consume bytes from an
invalid guest destination. This can block mapping changes during disk I/O and
limits parallel file throughput; correctness comes before finer-grained locks.
Test partial copies and the no-copy EOF case before adding adaptations. No file
mapping or executable-memory support is implied by this descriptor stage.

## No-JIT cost ledger

| Constraint | Consequence / evidence |
| --- | --- |
| Prepare native code before signing | The M1 fixture is transformed and signed at build time; adding native code requires a rebuilt, signed bundle. General native libraries remain unsupported. |
| No ART JIT or runtime code patching | Use the DEX interpreter or signed host-produced AOT; slowdown not measured. |
| No executable anonymous mappings | Guest executable `mmap`/`mprotect` requests fail explicitly. |
| Fix ABI/TLS/syscalls before install | Native input compatibility is constrained and must be tested. |
| AOT depends on ART/boot image/compiler versions | Pin the complete AOT provenance. |
| No runtime-generated native trampolines | Use precompiled ABI bridges or supported DEX interpreter paths. |
| Guest TLS must coexist with host TLS | The Bionic source profile uses a precompiled call and host TLS lookup; runtime cost remains unmeasured. |
| Android register and CPU-runtime assumptions cannot carry over unchanged | The current source profile omits Android SCS, uses a global stack guard and emits baseline atomics; performance and hardening tradeoffs remain to be measured. |
| Baseline string dispatch avoids IFUNC resolution | AOSP's static ARM64 dispatcher loses CPU-specific selection; guarded-page correctness is tested, and the performance difference remains unmeasured. |

## ADR 0024 - Native non-executable file views with independent lifetime

Status: all 43 mapping cases pass signed macOS and both Linux profiles at `b1a94c5`.

M2 needs file-backed data mappings whose shared changes and private copies match
Linux. Copying file contents into anonymous buffers would lose that behavior.
Use native POSIX mappings behind portable reference/map/sync callbacks. Duplicate
the backing descriptor while the VFS holds its lock, then retain it in VM metadata
until the reservation ends. The guest descriptor may close independently.

Per-page metadata preserves backing kind and write ceilings when protections
change or anonymous fixed replacements split a file reservation. Discard remaps
file pages from their original offset; anonymous pages retain demand-zero reset.
The implementation only maps non-executable data and never targets signed code.
Initially file MAP_FIXED and mixed-reservation operations return unsupported;
no unowned address can be replaced. A 43-case shared NDK caller tests observable
semantics on signed macOS and native Linux; portable tests inject backing errors
and verify reference cleanup. Native faults remain an unfinished guest-signal
boundary. No Linux implementation code is copied.

## ADR 0025 - Validate ELF versions independently of load-group scope

Status: local and CI parser/lookups and NDK/LLVM comparisons pass at `0184ec4`.

Decode DT_VERSYM, DT_VERDEF and DT_VERNEED through bounded file spans without
requiring section headers. Each image owns its numeric version indices; retain
names, hashes, provider SONAMEs, flags and hidden bits. Validate forward chains,
auxiliary entries, counts, strings, hashes, duplicate indices and symbol roles
before publishing a view. Bound the image to 256 version records.

Named lookup admits a matching hidden version; unqualified lookup selects an
export without the hidden bit. Like the pinned AOSP linker, an unversioned DSO
or global symbol may interpose on a versioned request. The load-group owner
must also resolve each specifically named dependency in the manifest. Version
matching follows AOSP: a candidate without the requested definition may only
provide an unversioned/global symbol, never an unrelated named version. Metadata
validation alone does not implement that dependency check.

Original NDK fixtures export two versions of one name and import both. Compare
all records and symbol assignments against LLVM under GNU, SysV and dual hash
tables and after removing section headers; verify each named export lookup.
LLVM 19 prints an empty text-only Predecessors field inside its JSON output;
the test strips only that exact empty field and retains the raw reference.
AOSP linker sources were studied for behavior; their code was not copied.

## ADR 0026 - Manifest-scoped linking with atomic group relocation

Status: all 19 host contracts and signed Bionic integration pass at `2ef7198`.

The platform loads and verifies signed bundle wrappers, then hands immutable ELF
metadata and writable data views to a portable load-group engine. Only manifest
basenames satisfy DT_NEEDED; no guest name enters the host library search path.
Resolve the root's breadth-first dependency closure, deduplicate shared children,
and retain deterministic traversal through cycles. Unreachable manifest entries
cannot supply symbols. A single group is bounded to 64 entries and 256 MiB of
staged writable data; later groups/namespaces will need an explicit global scope.

Resolve references by name and version with DT_SYMBOLIC self priority, normal
visibility rules, first eligible definitions (including weak interposers), and
explicit precompiled host exports after the guest scope. Missing weak imports
retain ELF zero binding; missing strong imports fail. Each DT_VERNEED provider
must exist in the resolved DT_NEEDED graph. AOSP permits unversioned/global
interposition; no unrelated named version may satisfy the lookup.

Stage every writable segment and finish every relocation before copying any
module back. This adds temporary data memory but prevents half-linked groups.
Reject overlapping writable views and aliases into immutable ELF inputs.
Constructor pointers must land inside a registered executable file span; validate
the complete call list before execution. Initialize dependencies before parents,
visit cycles once, and make repeated/reentrant initialization idempotent. A
failed guest callback poisons initialization. Guest threads must be stopped before
an owner destroys the group; destruction does not unload native wrappers.

Tests cover a diamond with a cycle, shuffled manifest order, an unreachable
export, weak and strong references, DT_SYMBOLIC, explicit bridge imports, late
relocation rollback, invalid constructor pointers, repeated initialization and
callback failure. The existing signed Bionic client now uses the same engine.
ELF TLS, preinit arrays, unloading/finalization and dlopen scope growth are not
implemented by this initial load-group step.

The next execution fixture adds a versioned third library and calls both the
hidden old and default new export from the NDK client. The exact provider ELF
and caller object also run under the native Linux ARM64 linker. This tests
version-aware scope relocation with distinct per-image version indices, rather
than relying only on per-image metadata assertions. Both signed Bionic profiles and native Linux pass at `5c8274d`; all CI is green.

## ADR 0027 - Guest ELF TLS through Bionic and precompiled TLSDESC access

Status: signed Bionic and original Linux ELF TLS pass at `32121e5`; all CI is green.

Register PT_TLS templates with module IDs in dependency-scope order, then let
Bionic reserve and initialize their static storage in each native guest thread.
Keep Bionic's DTV and `__tls_get_addr`. Preserve Darwin's thread pointer and x18.
TLS symbol values are module offsets, never ordinary load-biased addresses.

The selected first access path compiles controlled source with global-dynamic
ELF TLS and disables linker relaxation. At build time, replace compiler-emitted
TP reads with zero-base instructions; a precompiled TLSDESC resolver returns the
absolute guest address. The original assembly/object remains available for a
native Linux oracle. Reject other TLS access models and unexplained TP accesses.
This is a source build adaptation, not an arbitrary APK binary translator.

The alternative is compiler emulated TLS, with a lookup on every access and no
standard PT_TLS proof; retain it as a future fallback. Direct host TP substitution
would corrupt the host ABI and is excluded. Faster static-offset TLSDESC access
can follow measurements. The first bridge saves full SIMD registers around the
real Bionic accessor, adding stack traffic and a function call per access.
The [Arm TLSDESC convention](https://github.com/ARM-software/abi-aa/blob/main/sysvabi64/sysvabi64.rst#calling-convention)
requires preservation beyond the ordinary C calling convention.

The portable relocator prepares both descriptor words and checks sixteen-byte
bounds/overlap before publishing either. The group owns stable descriptor
arguments, drops them on failed group relocation, and requires threads to stop
before destruction. It validates the precompiled resolver against signed RX
ranges. Templates use relocated writable initialization bytes where present.
Ordinary address lookup rejects TLS symbols. Preinit, later dlopen TLS modules,
TLS unloading and other ELF TLS relocation models remain future work.

The native fixture now has TLS in two shared images: initialized and 32-byte
aligned zero storage in a provider, and a 64-byte aligned local template in the
client. Main plus six real Bionic pthreads check initial values, zero fill,
alignment and persistent per-thread mutations. Original compiler output runs
under Linux's linker with six concurrent pthreads. A shared assembly acceptance
function checks x2-x17 and all 32 full SIMD registers across two resolver calls,
covering initial DTV allocation and a subsequent access. The signed resolver
also preserves x1, LR and NZCV. No code or third-party implementation was copied
for these fixtures. Dynamic DTV allocation uses Bionic's existing allocator;
static TLS lifetime follows Bionic thread mappings and the native reaper.

## ADR 0028 - One native acceptance runner for macOS and iOS

Status: shared native runner, Linux comparisons and integrated M2 IPA pass at `545f8b6`; downloaded IPA verified.

The macOS test executable and the iOS console call the same Apple platform
acceptance runner. Only argument collection and log presentation differ. Keep
the portable loader, memory, filesystem and thread engines in the core. The
runner remains diagnostic and single-use: unexpected faults or failed contracts
terminate the process, and it is not a recoverable APK runtime API.

The host workflow builds/signs all four iOS frameworks while testing their Mac
counterparts. After the host and Linux oracle jobs pass, a device job consumes
that exact run's artifact, checks its project revision, ELF/layout hashes and
notices, then builds a real iOS 15 target. Embed the original ELF metadata as
read-only bundle resources and verify the copied framework signatures and bytes.
The iOS console creates a fresh rooted filesystem and runs the normal suite on
a background queue. Forced-sampling execution remains a separate Mac process.
Physical execution is still unverified and is not a milestone gate.

## ADR 0029 - Freeze the final M2 expectations and pair remaining cases

Freeze 328 expectations in the versioned acceptance manifest before virtual proc
implementation and final acceptance reporting. Existing numbered memory/file
checks retain their counts; thread, TLS, version and constructor workloads are
whole contracts, without counting retries or repeated iterations as new tests.
The contract documents when this numerical denominator was fixed; the earlier
area requirements remain the design basis. All groups are mandatory and aborted
or unexecuted cases cannot count as passing.

Run the 35-case anonymous-memory object inside full dynamic Bionic as well as the
existing slice, and test each exact object through the native Linux Bionic stubs.
The 18 timeout expectations compile from identical source against each runtime's
pthread headers: opaque pthread storage is not shared across libc ABIs. Cover
realtime/monotonic deadlines, invalid nanoseconds, mutex reacquisition and actual
expiry, allowing spurious wakeups until the original deadline.

Establish the 22-case proc caller on native Linux before implementing its virtual
nodes. The [Linux cmdline operations](https://github.com/torvalds/linux/blob/master/fs/proc/base.c)
read argv through the current file offset and use generic llseek, with stat size
zero. The first ARTBox path will serve an immutable initial-argv snapshot, with
live argv mutations, setproctitle and broader proc files explicitly unsupported.
No kernel source is copied.

## ADR 0030 - Owned initial-argv proc snapshot and explicit acceptance scoring

Status: both Bionic profiles pass 328/328 at `e50ec7f`; host, Linux and integrated
iOS CI are green and the downloaded IPA is verified. See [M2 acceptance](acceptance/m2.md).

Use the existing portable VFS descriptor table for proc data. Copy the initial
argv bytes once (maximum 64 KiB), assign a separate cursor per open, and keep
stat size zero even when readable bytes remain. Resolve the followed self alias
inside the guest namespace; never fall through to host proc paths. Preserve
partial-copy and EOF semantics. An immutable snapshot costs one bounded copy
and does not reflect later guest argv mutations; accept that initial limitation.

The exact original NDK proc caller first passed native Linux, including SEEK_END
at the zero inode size. It now runs through the same real Bionic syscall path as
other signed fixtures. Portable tests separately exercise ownership and partial
faults. Unknown proc paths, writable command-line access and file mappings are
rejected; broader proc features remain explicit gaps.

Score the frozen 328-expectation manifest after both native profiles complete.
A numbered caller must return its exact count; each whole workload must satisfy
all required result fields. Missing fields retain unexecuted cases, and extra or
inflated counts cannot increase the numerator. A mandatory workload failure
rejects acceptance even above 90 percent. The device packaging step recomputes
this score and verifies the canonical JSON manifest hash before consuming any
artifact. No physical-device result is inferred from a signed build.

## ADR 0031 - Validate ART's low-address heap before adapting its references

Status: the reduced guard was rejected before entry on native ARM64 macOS at
`87b036f` (EBADMACHO). At `0f1aed8`, the signed native comparison and
high-address codec pass CI; both iOS 15 layouts/signatures are verified.

ART at Android 15 stores managed references as 32-bit addresses in
`runtime/mirror/object_reference.h`. Our current app's 4 GiB `__PAGEZERO`
occupies that address range. First compare the default guard with a 64 KiB
guard using the public `-pagezero_size` linker option, before modifying ART's
object layout or compressed-reference encoding. Keep all mappings non-executable
and use address hints without fixed replacement. Verify two independent 64 MiB
reservations, zeroed end pages, reference round trips and release.

The alternatives are a heap-base-relative reference representation (more invasive
changes to ART, native bridges and future AOT output), wider references (layout
and memory cost), or accepting a slower managed path with explicitly adapted
references. No CPU emulator, kernel or entitlement workaround is an option.
The probe does not select the production layout until its native and signed
device-build results are known; the current app layout remains the baseline.


The native failure rules out the reduced-guard route for ARM64. Apple's
[XNU loader](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/mach_loader.c)
requires a 4 GiB hard page-zero region. Keep the default executable layout and
retain the rejected prototype as an explicit EBADMACHO test; other errors or
unexpected execution fail. This records the failed assumption without skipping
the address-space check.

Select heap-base-relative 32-bit byte offsets for initial switch interpretation.
Reserve zero for null and keep a guard at the beginning of one stable, at-most
4 GiB window. The first portable codec checks alignment, range and arithmetic
before publishing an output. It does not allocate, collect, validate object
liveness or modify ART yet. Native tests store and retrieve bytes through a
reference to memory above 4 GiB, alongside malformed/overflow/null boundaries.
An add/subtract and validation branches per reference are accepted initial
costs. ART's stack references, JNI roots, read barriers, image relocation and
future AOT output require a consistent adaptation; no compatibility with
unmodified ART native reference accesses is implied.


The negative launch harness calls public `posix_spawn` directly after checking
Mach-O layout and the ordinary signature. CTest initially reported EBADMACHO;
Python's subprocess wrapper instead observed a killed child, including with an
explicit pre-exec callback. A generic signal is not accepted as proof of this
loader restriction. Keep the exact errno check and the independent high-address
reference success test.

## ADR 0032 - Validate real ART reference storage before widening the runtime build

Status: at `2586a50`, 38 signed Mac cases and 38 original Linux cases pass,
including stored representations and real acquire/release instructions.
Both iOS framework profiles pass signature/layout checks; see
[reference evidence](m3-references.md).

Use the pinned Android 15 reference types themselves before adapting the full
interpreter. Select their 12-file ART header closure, five libbase headers and
seven fmtlib headers, with each component's complete reviewed notices. The
original ARTBox contract was compiled against unmodified AOSP headers before
the compression overlay was added. Compare those original NDK objects on native
Linux with adapted NDK objects packaged by the existing signed ELF wrapper.

Change only the raw-pointer `PtrCompression` entry points. Retain AOSP's null,
copy, four-byte storage, stack-vreg, volatile access and negation-based poison
semantics. Test heap poisoning both off and on. Keep the source cache immutable;
reject a changed upstream hash or repeated adaptation. Preserve the Apache
notice and label the change in the generated overlay. Every packaged object
must have no SVC, TPIDR, reserved-register or unknown instruction references;
only the two fixed-width reference bridges may remain as native imports.

The diagnostic bridge calls the checked portable codec for every access, which
adds native calls as well as range/alignment validation. This is an accepted
initial cost, not an optimized production reference ABI. It uses one stable
window and aborts on invalid input. Storage used by this fixture is opaque
aligned memory, not constructed ART Objects. The fixture verifies decoded
reads/writes but establishes neither object liveness nor collector correctness.

`ObjPtr` debug encoding, its reference overloads, CAS operations defined in
other headers, GC/root/JNI access, read barriers, class layout, quick/nterp and
AOT code remain outside this test. Each needs consistent encoding and actual
runtime tests before claiming ART support. iOS framework compilation and
signature/layout checks are separate from device execution.

The first native comparison at `89aa2b7` passed, but artifact inspection showed
that optimization folded away most local storage and both poisoning profiles
had identical machine code. Strengthen the test before relying on its volatile
coverage: construct the actual HeapReference in caller-provided aligned storage,
read representations through volatile bytes, and return four storage observations
for independent host checks. Require real ARM64 `ldar` and `stlr` instructions
in both original and adapted objects. This changes the diagnostic fixture, not
AOSP's atomic implementation or the selected compression adaptation.

## ADR 0033 - Establish an upstream DEX loading baseline before runtime adaptation

Status: at `28b13ff`, the six native format cases pass on Mac and Linux ARM64,
and the iOS 15 library links and passes signature/layout checks. See
[DEX loading evidence](m3-dex.md). ART/DEX execution remains unimplemented.

Use AOSP's normal `DexFileLoader` API with structural and checksum verification
enabled. An original Python generator emits a fixed 448-byte DEX with one
static string-returning method. The independent AOSP verifier and accessors
check its tables, method and instruction data. Mutated checksums, map entries,
method-name indices, code sizes and truncation must return ordinary failures.
Merely observing a string constant does not count as method execution.

Build the portable upstream library with each host's C++ ABI for this format
baseline. Keep C++ types inside that build and expose only a fixed C entry.
The same source selection must also link and sign for iOS 15, which surfaces
SDK restrictions early. Separately compile its Android ARM64 units on Windows;
that result provides source/build evidence and does not claim native loading.
The full ART/Bionic runtime and its Android C++ dependencies remain subsequent
integration work. This baseline does not substitute host C++ symbols into an
Android ELF or change the native guest ABI.

Select 108 ART files, the needed libbase and ZIP reader support, three host
liblog units, the existing filesystem-ID header, fmt headers and the AOSP JNI
type header. Reuse AOSP's enum-printer generator. Android 15's file support
requires declarations hidden at the exploratory API 28 target; use Android API
35 for its compile check, preserving fdsan calls. The Apple deployment target
remains iOS 15. No fdsan runtime behavior is established by this compile check.

The host baseline uses the upstream options disabling the ZIP callback API and
IncFS signal support. It reads complete fixture files; kernel incremental-file
semantics are outside its scope. Managed-heap allocation, class libraries,
runtime signals and JIT/AOT paths are not part of the DEX format result.

The first Mac format checks and iOS library build pass, but native Linux with
libstdc++ rejects `time_utils.h`: it uses `std::numeric_limits` before
`time_utils.cc` includes `<limits>`. Keep the failing Linux compile check and
apply one hash-checked source overlay, moving that include before the header
and explicitly including `<algorithm>` for `std::min`. The generated source
retains its Apache notice and labels the include-order change; the source cache
is unchanged. Use the same overlay on all targets and compare its hash in the
Mac/Linux evidence. No time behavior or verifier check changes.

The next Linux compile reaches libbase's POSIX `strerror_r` wrapper, which
intentionally undefines `_GNU_SOURCE`. In strict C++ mode glibc also needs an
explicit feature level to expose that declaration. Build Linux with
`_POSIX_C_SOURCE=200809L`, retaining the upstream POSIX return ABI and source.
Preserve per-unit compiler diagnostics with the Linux artifact for subsequent
dependency failures.

## 0034: Fetch large source selections from a pinned archive

Status: accepted for M3 build preparation.

The ART class libraries require thousands of Java source files. Fetching each
file through GitHub's contents API adds thousands of requests to a fresh build.
Allow an existing exact file selection to specify a reviewed archive hash and
size as its transport. Stream only selected regular files into a temporary
tree, then verify all file hashes and notices before installation. Revalidate
cached selected files on reuse, as with individual-file downloads.

This retains a larger compressed archive in the configured cache in exchange
for one download per component. Unselected files and aliases are not installed.
Tests cover selection, cache corruption, archive corruption, traversal, missing
files, duplicate/case-conflicting entries and selected aliases. Existing small
selections continue to support individual-file downloads.

Reviewed binary build tools can specify a Git blob identifier alongside their
file hash. Fetch and decode the API's base64 representation to preserve arbitrary
bytes through CLI output, then check the Git identity and SHA-256. A binary
fixture containing invalid UTF-8 and zero bytes tests this path and rejects
corrupted content before installation.

ADR 0052 later replaces the REST transport with public raw/codeload downloads;
the pinned binary identity and file-integrity requirements remain unchanged.

## 0035: Build implementation class libraries from selected AOSP Java sources

Status: accepted for the M3 class-library prerequisite; runtime boot pending.

ART needs implementation classes rather than SDK signature stubs. Select the
libcore implementation source groups and only their required Java dependencies
from Android 15's named tag. Build them with an explicitly selected JDK 17,
without its host boot classes or annotation processors. Follow AOSP Soong's
inline string-concatenation option and libcore's minimum Android API 31 for D8.
This avoids a full Soong checkout and does not change the iOS 15 target.

Generate Conscrypt's constants with the original upstream generator and pinned
BoringSSL headers; compare all 50 values. Generate ICU annotation keys from the
hash-checked aconfig declarations, without introducing runtime flag stubs.
Keep these generated inputs, corresponding selected source, original notices
and portable build commands with every class-library binary artifact. Original
ARTBox files remain MIT; upstream source licenses remain in force.

Use one implementation input jar and allow D8 to emit multiple DEX files. This
first format baseline does not reproduce the final bootclasspath module split,
hidden-API metadata, resources or JNI libraries. Do not call it an ART boot.
Validate the complete class set with AOSP's DEX verifier on native Mac/Linux,
including corrupt input and duplicate/missing DEX cases, and compile the same
inspection entry for an ordinarily signed iOS 15 framework. Interpreter-only
runtime integration is the next contract; no runtime code generation is used
to prepare or inspect these DEX data files.

The first class-library CI run selected JDK 21 from the runner environment
instead of JDK 17. Keep the JDK 17 build contract and add an explicit
`--fetch-jdk` option for a checksum-pinned portable distribution. Download and
extract it inside the configured cache; retain its complete licenses and
revalidate installed file hashes. CI opts in, while interactive builds can
continue to select an existing JDK. Archive tests cover binary preservation,
executable permissions, internal aliases, traversal and duplicate entries.

## 0036: Reject compiler creation and omit optional Rust trace formatting

Status: accepted for M3 bring-up; complete runtime enforcement pending.

Use the original AOSP `jit_create()` declaration with an ARTBox definition that
logs the violation and terminates the diagnostic process with exit 126. Do not
return a null compiler interface: the caller immediately dereferences it. This
keeps the compiler library out of the interpreter build and makes an unexpected
factory call observable. A native child-process test verifies the diagnostic,
exit code and absence of a return to the caller.

The factory is a final invariant check, not permission to create a code cache.
`Runtime::CreateJit()` allocates the cache before calling the factory. Runtime
integration must disable both compilation and profile saving, select the
interpreter and reject executable-memory creation at the platform boundary.
Those complete startup checks remain required for M3 acceptance.

Retain upstream C++ stack-trace demangling but make Rust formatting optional
through an explicit build flag. Preserve Rust labels verbatim in the first
interpreter build. This avoids importing a Rust standard library only for
diagnostic names; adding the original Rust demangler later remains possible.
Hash-check and label the source overlay. Eight native name cases and an
unchanged-source negative control verify this behavior before integration.

The Windows-only fmt stream header requires RTTI even when unused. Allow RTTI
in the Windows native policy fixture; Apple policy builds and Android runtime
objects retain their existing no-RTTI settings. No exception behavior or runtime
code generation is introduced by that host compilation choice.

## 0037: Preserve a native Linux reference during ART ABI adaptation

Status: accepted for build bring-up; runtime execution pending.

Compile the original interpreter and support sources before adapting their
memory representation or managed entrypoints. Keep an independent native Linux
host reference with the original reference representation and host C/C++ ABI.
The Apple-bound Android build uses Bionic's tested native TLS bridge; it still
needs complete reference, signal and register-boundary validation.

A static NDK/Bionic reference link collides with ART's real `signal` interposer.
A shared link against the public NDK libc ABI lacks `android_mallopt` and
`async_safe_format_log_va_list`, which the selected sources reference. Do not
resolve these by allowing duplicate symbols or substituting success stubs.
Use the upstream Linux host configuration for reference execution and the
source-built Bionic dependency set for Apple integration. Each configuration
must compile its dependencies consistently; C++ library ABI objects cannot be
mixed across them.

Record the required runtime/support source selection in a separate pinned
catalog, so existing smaller fixtures do not download the complete runtime.
Reuse the existing hash-verifying downloader and configurable cache. Include
the upstream assembly generators and templates, and generate outputs inside
the build directory. Preparing this catalog does not satisfy M3 execution.

## 0038: Adapt ART's host assumptions to the selected Linux C library

Status: accepted for build bring-up; complete runtime execution pending.

The first complete native Linux build compiled 450 of 458 units. The remaining
failures exposed missing standard includes, an unqualified `nullptr_t`, a
`strlcpy` fallback that conflicts with current glibc, and assumptions that
thread/signal stack minima are unsigned compile-time constants.

Use explicit declarations and qualify the standard type. Probe `strlcpy` with
the selected compiler and library, and check copying, truncation and zero-size
behavior through ART's header. Keep the upstream fallback for hosts without the
function. Validate dynamic stack minima before conversion to `size_t`, preserve
the requested floor, and cache the alternate-stack size at first use. Invalid
system minima fail explicitly. Keep upstream warnings enabled.

Hash-check and label each source edit. Copy the verified selection into the
build directory so sibling quoted includes see the adapted headers. Preserve
the unchanged upstream sources, notices, replacement manifest and adapted files
with binary artifacts. These host-build fixes do not establish Apple TLS,
signal handling or managed-reference compatibility.

## 0039: Build the real ICU and JNI dependencies before ART startup

Status: accepted for dependency bring-up; ART startup pending.

ART loads ICU JNI before the native libcore libraries. Preserve their real
upstream implementations and use the matching ICU 75 data from the pinned AOSP
archive. Keep the data root configurable through the original registration
code's `ANDROID_I18N_ROOT` interface. A native check must exercise ICU data and
load the JNI library; finding `JNI_OnLoad` alone cannot establish registration
or Java execution.

For the Linux reference, reuse base/log support symbols already exported by
the monolithic ART library. Verify that dependency against its build record and
preserve its complete corresponding source with the native library artifacts.
Apple packaging and native ABI adaptation remain separate acceptance work.

Retain ICU's upstream RTTI setting and disable C++ exceptions. No code-generation
facility is introduced. The selected data file adds about 28 MB before packaging;
its mapped size is not a resident-memory measurement. Measure interpreter and
application memory after the complete runtime can start.

## 0040: Preserve the original native class-library implementations

Status: accepted for dependency bring-up; Java execution pending.

Build libcore's original JNI implementations and ART's OpenjdkJvm bridge before
attempting boot-class initialization. Keep fdlibm and `libcrypto_for_art` as
static archives, as upstream does, so unused members do not introduce unrelated
crypto dependencies. Expat remains a shared library. The Linux reference reuses
the base/log, ZIP and zlib symbols in its verified monolithic ART library.

Reuse ART's actual compiler and header configuration for OpenjdkJvm. Preserve
the original sibling layout for its libcore include and the original fdlibm
header at StrictMath's expected relative path. This avoids editing upstream
sources just to replace their build system. Check every selected file and
retain the layout recipe with complete corresponding source and notices.

Exercise native dependency behavior before VM registration: deterministic
crypto/math vectors, malformed XML, file errors and monitor contention, plus
eager JNI library loading. These tests cannot establish ART or Java execution.
Portable BoringSSL C code avoids adding an assembly ABI boundary during bring-up;
its performance cost remains to be measured with an actual application.

## 0041: Supply explicit Linux host declarations and JNI symbol scope

Status: accepted for native libcore bring-up; execution checks pending.

The first Linux libcore build passed 171 of 208 compilation units. Its failures
exposed hidden pthread declarations in strict C11, OpenjdkJvm's missing math
header, and a capability header unavailable in the runner's glibc development
files. Enable GNU declarations for the Linux BoringSSL C build and explicitly
include the standard math header for OpenjdkJvm.

Reuse Bionic's original capability declarations with Linux's own kernel types.
Forward `capget` and `capset` through the actual host `syscall` interface. The
[Linux interface documentation](https://man7.org/linux/man-pages/man2/capget.2.html)
records glibc's lack of these wrappers. Test reads, invalid versions, null
headers, errno and output mutation against raw syscalls; no valid capability
mutation is part of the test. This bridge adds no Android or iOS privilege.

Preserve AOSP's original javacore export map. Both native class libraries define
the same C++ class-cache names for different class sets, so javacore must keep
its implementation local. Check that its JNI entrypoint remains visible and
its class-cache initializer cannot be found through the public dynamic scope.

## 0042: Activate the original ICU shim header configuration

Status: accepted for native libcore linking; startup pending.

The first javacore link requested versioned `_75` ICU symbols while linking
the unversioned `libicu` C API shim. The selected NDK headers include their
original `uconfig_local.h` only for an AOSP (`ANDROID`) or Android-target
(`__ANDROID__`) build. That local configuration selects the shim's symbol ABI.

Set the AOSP build marker for libcore's ICU header consumers while preserving
the target-OS selection. Retain the implementation libraries' own versioning
and the original shim between the two APIs. Do not rename exports or alter
the ICU sources. Exercise the shim directly through the same configured
headers, including version lookup and malformed UTF-8 substitution.

## 0043: Deny code generation in the native ART startup reference

Status: native Linux ARM64 hello and policy checks verified at `7d0ed24`.

Preload the pinned native libraries and then install a Linux syscall filter
before calling original JNI_CreateJavaVM. Reject executable-memory requests and
runtime process execution; synchronize existing threads and test inheritance.
Use the real switch interpreter with both JIT compilation and profiling disabled,
and assert the actual runtime policy before and after method invocation.

Preserve the same-revision libraries, implementation DEX, logs and mapping
permissions with each attempt. This catches hidden code-generation dependencies
while preparing signed Apple integration. The filter is a native reference test
instrument, not a replacement for iOS signing or a complete app sandbox.

## 0044: Match ART's D8 class-library layout configuration

Status: accepted; native Linux bootstrap passes at `7d0ed24` with ADR 0045.

The first original VM invocation reaches imageless bootstrap and fails ART's
system-class identity check for `java.lang.String`. Both ART and libcore use
the pinned Android 15 release, but the runtime builder omitted the upstream
`USE_D8_DESUGAR=1` setting while the class-library builder uses D8. Enable that
setting for the runtime and its JNI harness. In the pinned `build/art.go` it is
the default; `mirror/string-inl.h` uses it to account for the two CharSequence
lambdas that D8 represents as direct methods.

The original ARM64 size expression changes from 824 to 808 bytes with this
setting, while the failed run reports a 792-byte linked class. Do not infer
that the flag resolves the whole failure or replace the constant with 792.
Retain the system-class check and add diagnostic output for the actual linked
header, embedded vtable length and static field offsets through the verified
host-source overlay. The native diagnostic at `67f0219` confirms a 120-byte
header and 79 vtable slots. ADR 0045 records the second omitted build setting.

## 0045: Preserve AOSP's automatic-storage initialization policy

Status: accepted; native ART startup and hello pass at `7d0ed24`.

The pinned class linker's `AssignVTableIndexes` allocates a small buffer with
`alloca` and passes part of it to `BitVector`. That constructor retains the
provided bits. For the current String DEX, the stack branch uses 255 words;
stale bits can therefore suppress assignment of otherwise new virtual methods.
The runtime builder omitted AOSP's global automatic-storage initialization flag.

At `67f0219`, native diagnostics find 79 embedded vtable slots instead of the
81 implied by the DEX. The 120-byte header and static offsets account for the
remaining 16-byte discrepancy after enabling D8 configuration. This is consistent
with stale bitmap bits.

Enable `-ftrivial-auto-var-init=zero`, matching the Android 15 default in
[Soong's compiler configuration](https://android.googlesource.com/platform/build/soong/+/refs/tags/android-15.0.0_r1/cc/config/global.go).
The pinned Android compiler emits a zeroing operation for explicit `alloca`
with this flag. Preserve the original linker and bitmap implementation instead
of adding an isolated clear that would miss other users of the same build policy.

Before the native ART build, test both an automatic array and four dynamic
allocation sizes with the actual compiler and runtime flags. Compile the same
probe with pattern initialization as a deterministic negative control; all five
cases must be nonzero there. Retain both results and require them before VM
startup. The probe passes on native Windows using the pinned Clang compiler;
both controls also pass on native ARM64. At `7d0ed24`, the original VM completes
bootstrap and executes the hello DEX, resolving the observed String mismatch.
Zeroing adds stack writes; their isolated runtime cost is not yet measured.

## 0046: Verify managed collection and native attachment through original ART

Status: verified on native Linux ARM64 at `7f9d8da`; Apple runtime integration pending.

Keep the original hello DEX and add an original Java fixture for allocation,
cyclic references, array contents, virtual dispatch and null/bounds exceptions.
Compile it separately with the pinned JDK/D8 tools, retaining source, notices,
commands and hashes. Check the exact producer revision and current source hashes
before adding the DEX to ART's application class path. Host JDK test execution
only checks fixture logic; it never substitutes for ART acceptance.

Use `Runtime.gc()` and observe the existing VMDebug collection counter through
JNI. The pinned `System.gc()` may defer collection for target SDK <= 34. A counter
increase and preserved managed roots provide evidence beyond requesting a GC;
no new native GC API or collector stub is introduced.

Exercise two native threads, each attaching and detaching twice, with `GetEnv`
transitions, managed thread names and a synchronized Java counter. Detach the
launching thread before destroying the VM and verify that VM registration is
empty afterward. This tests the original destructor's shutdown-thread path and
addresses the warning observed in the first successful hello run. Keep the
code-generation denial filter active through all methods and shutdown, and
retain mapping snapshots after both managed calls and VM destruction.

The native run observes one explicit semispace collection and preserves the
graph/checksum, catches both exceptions, completes all four attachment cycles
and leaves no registered VM after destruction. The earlier attached-thread
shutdown warning is absent. All 18 executable mappings remain unchanged through
shutdown. Measurements and artifact provenance are in
[the managed acceptance record](m3-managed-checks.md); this does not establish
Apple runtime or physical-device execution.

## 0047: Encode GC forwarding addresses and preserve raw JNI dead markers

Status: validated in native managed-storage tests; full-runtime integration pending.

The original semispace collector writes an object's destination into LockWord.
The pinned implementation truncates a native address to 32 bits on decoding;
changing ObjectReference alone cannot move the heap above Apple's guard region.
Use the same checked byte-offset codec before packing a forwarding address and
after unpacking it. Ordinary lock, hash and state-bit formats retain their tests.
This adds checked codec calls on forwarding paths; collector overhead must be
measured when the complete runtime uses the high heap.

LocalReferenceTable writes a removed-entry marker through SetReference even in
release builds. That value is not a pointer in the managed heap. Add a dedicated
raw-marker setter and use it at all three removal/pruning sites, preserving the
existing marker bits. Do not admit arbitrary low addresses into the heap codec.

Test the actual pinned storage types before integrating these changes into ART:
the original signed high-address control must fail at forwarding round-trip case
five, adapted signed Mac code must pass 27 cases per poisoning profile, and the
same original NDK ELF must pass below 4 GiB on native Linux. Include signed iOS
frameworks and source provenance. These tests do not execute an Apple collector;
interpreter arguments, stack walking, the heap window and native entrypoints
still need consistent representation. See [the storage contract](m3-managed-storage.md).

At `773da40`, both hosts pass all 54 positive cases and both signed Mac controls
fail at the expected forwarding case. Downloaded sources and binaries verify.
The test establishes the storage encoding, not a moving Apple collector.

## 0048: Classify interpreter arguments using their encoded reference value

Status: validated in native argument-copy tests; full-runtime integration pending.

After ObjectReference becomes heap-relative, AssignRegister must compare a raw
vreg with the encoding of its reference slot. Comparing with a truncated native
pointer loses the callee's GC root. Use the existing checked codec, preserving
the original handling of nulls and primitives beside stale references. This adds
codec calls to argument copying; measure their cost after full-runtime integration.

Compile the real interpreter_common.cc in the test, retaining the actual
ShadowFrame and copying helpers. Test the original ELF on native Linux and
signed Mac payloads with only the reference change and with both changes. The
partial adaptation must fail at the first copied reference; both fully matching
representations must pass. Keep both heap-poisoning profiles and an iOS 15 build.
This validates one interpreter boundary, not complete managed execution or GC.
See [the argument-copy contract](m3-interpreter-arguments.md).

At `5472832`, the original Linux and fully adapted signed Mac payloads each pass
36 cases. Both partial-adaptation controls fail at case four as required. The
downloaded source, ELF and framework hashes verify. No collector runs in this test.

## 0049: Keep managed heap holes inside one owned native reservation

Status: standalone window validated in native CI; ART integration pending.

A stable reference base requires allocation and unmapping to agree on ownership.
Add a window mode to the existing VM mapper instead of a separate allocator with
different permission and failure rules. Reserve at most 4 GiB once, exclude the
leading guard, allocate contiguous holes under the mapping lock, and retain
freed pages as inaccessible reserved storage until destruction. Fixed replacement
cannot escape that reservation. Reuse the existing mutation-failure poisoning.

Initially support anonymous storage for imageless startup. Reject file mappings
and borrowed ranges explicitly until their actual ART callers and ownership
requirements are covered. Test real high-address accesses, concurrent reuse and
the codec's final eight-byte slot before adapting ART's MemMap and card table.
Linear hole search and the per-page metadata cost require runtime measurements;
a successful host reservation does not establish an iPhone memory budget.
See [the heap-window contract](m3-heap-window.md).

At `3ad7f50`, full host and iOS build CI pass. Native portable tests cover the
standalone window on Mac, Linux and Windows; this does not execute ART in it.

## 0050: Share managed-page ownership with the syscall address space

Status: validated in native CI at a700398; ART integration execution pending.

Bionic's syscall bridge validates guest pointers against its existing VM
registry. A separate managed allocator would make valid heap buffers fail that
validation. Registering an entire reservation as borrowed data would instead
admit inaccessible guards and freed holes. Attach retained windows to the
existing registry, and keep mapped-page metadata under the same lock as I/O,
protection and unmap operations.

Select the managed window explicitly when allocating heap pages. Ordinary
non-fixed Bionic mmap remains outside it; Scudo's reservations must not consume
the managed reference range. Keep retention and guard size on each region so
ordinary mappings still release when empty. An explicit fixed window allocation
must stay inside that selected window, while the normal syscall dispatcher can
replace mapped or free pages inside any owned range except its guard.

Test syscall copyout, denied I/O callbacks for holes, two distinct windows,
borrowed ranges, limits and mutation failure before adapting ART's MemMap.
Anonymous-only heap allocation is sufficient for the intended imageless bring-up;
file-backed image maps require a separate tested extension.

## 0051: Run the complete high-heap ART variant beside its original reference

Status: source integration implemented and ARM64 compilation checked; execution pending.

Focused storage tests cannot prove that every live ART caller uses the same
representation. Build a second full runtime with the reviewed reference,
forwarding, JNI-marker and argument-copy changes. Route actual MemMap low-address
requests into the owned window, including direct tail remapping and protection;
retain unmapped holes and reject mremap ownership transfer. Set the real card
table's extent from the window. Keep the original full-runtime profile required.

Bind before starting the runtime and release after its complete shutdown. Permit
the null encoding independently of binding, since static null roots do not need
heap storage. Non-null pointers must still pass the checked codec. The initial
Linux profile owns its window through the same portable VM API that can attach
to the Apple Bionic syscall registry.

Compile native class libraries against each profile's actual headers/libart.
Test real MemMap/CardTable operations before entering JNI_CreateJavaVM, then
require the unchanged hello, GC, exception and native-thread acceptance suite.
Archive both profiles independently with their exact source edits and binaries.
This repeats compilation but preserves a useful comparison and catches inline
header ABI mismatches. Codec-call cost and the 4 GiB reservation's practical
budget remain unmeasured until execution. See [the profile](m3-high-heap-runtime.md).

The first native build compiles all 462 units and links libart, then rejects
the separate fixture's references to hidden CardTable symbols. Compile the
acceptance helper inside the test runtime library as a 463rd unit. This tests
the actual private implementation without changing AOSP's export policy.

## 0052: Fetch pinned public source bytes without consuming the REST quota

Status: live transport and local integrity tests pass; CI revalidation pending.

At `385a02d`, Mac host and class-library jobs fail during source downloads with
GitHub installation API rate-limit errors. The larger runtime matrix increases
concurrent fresh source acquisition. Use `gh api` with GitHub's public raw-content
URLs for selected files and codeload legacy archives for pinned tarballs. Keep
the immutable commit, file sizes, SHA-256, reviewed notices, safe staging and
cache revalidation. For binary pins, compute and check the recorded Git blob
identity from the raw bytes as well. Public source requests use an explicit empty
Authorization header; repository operations retain the configured credentials.

Live probes verify a complete 27-file liblog selection, the exact pinned 50,151-byte
compat archive and the 16,688,724-byte R8 binary including its Git blob identity.
Eleven extraction/transport tests retain corruption, traversal, incomplete install,
binary-byte and cache checks and add encoded-path coverage. No source selection,
license requirement or CI acceptance test is removed.
