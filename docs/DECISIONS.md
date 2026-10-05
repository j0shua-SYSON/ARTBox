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
ADR 0096 refines release to the last original file page, including anonymous
replacement of every file page within a still-live reservation.

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

Status: full high-heap hello, GC, exception and lifecycle acceptance passes at 71f398e on native Linux.

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

Status: live transport, local integrity tests and both native class-library CI jobs pass at 0979faf.

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

## 0053: Keep class-table hash tags while encoding heap-relative roots

Status: native slot regression passes at 5acd098; full high-heap GC/lifecycle also passes at 71f398e.

The first full high-heap startup at `0979faf` aborts in the checked reference
encoder during `ClassTable::Lookup`. Its four-byte TableSlot combines a native
pointer truncated to 32 bits with three descriptor-hash bits, then reconstructs
an absolute pointer before constructing a GcRoot. The original runtime passes
in the same CI run, while the adapted MemMap/CardTable preflight already passes.

Add a preflight regression using the actual TableSlot implementation before
changing this representation. Cover all eight hash tags, empty and raw-encoded
slots, copy/assignment, no-barrier reads, and visitor-driven unchanged, relocated
and cleared roots. Encode pointers with the checked heap codec before applying
the hash bits; strip the bits before decoding. The eight-byte object alignment
leaves the same tag space in the offset. Preserve slot width, atomic update,
hash comparison and null handling. Do not accept truncated pointers in the codec.

Keep the original source and notices, and pin these three textual edits in
`class-table-boundary.json`. The current scope is imageless interpreter startup;
serialized image tables and compiled-code consumers still need their own
acceptance before enabling those paths. Fatal codec diagnostics report the
offending value and window so subsequent representation failures are traceable.

## 0054: Place large-object live and mark bitmaps over the owned heap window

Status: constructor/bitmap regression and full high-heap managed acceptance pass at 71f398e.

After the class-table fix, `5acd098` starts the high-heap VM and runs the hello
DEX, then fails the existing managed fixture's explicit collection.
`LargeObjectSpace::Sweep` supplies high native addresses to bitmaps constructed
by DiscontinuousSpace for the low 4 GiB. The original SweepWalk bound check
rejects that mismatch. The issue is bitmap extent, not reference decoding.

Keep the real space and bitmap algorithms. Initialize both bitmaps with the
bound window's native base and capacity. Cover the complete reservation so its
size stays a bitmap-word multiple; the allocation guard remains reserved and
its unused bitmap bits stay clear. This does not make guards or freed holes
valid VM buffers. Add an actual constructor regression for extent and first/
last-page coverage, excluded outside addresses, live/mark independence, copying
and clearing before adapting the pinned source. The full managed GC test stays
required and SweepWalk's safety checks remain intact.

## 0055: Observe thread state from inside the ART runtime library

Status: both full native Linux runtime profiles pass at 8720bc8; Apple integration pending.

Apple integration must preserve Bionic's ART current-thread slot and compiler
TLS used by the heap sampler. Test the real runtime before adapting either
boundary. Compile the acceptance accessor inside libart so its inline hidden
sampler variable belongs to the runtime instead of a separate harness DSO.
Do not replace ART's actual attach/detach paths with fixture state.

Keep three native threads alive simultaneously while comparing distinct sampler
slots and temporary values. Verify null thread state before attachment, the
actual matching JNI environment while attached, and cleared state after each
of two detach/reattach cycles per worker. Preserve each thread's sampler address
and independent value across these cycles; restore the original values. Reject
this intrusive fixture if sampling is enabled. Both full Linux runtime profiles
must pass this contract alongside the existing Java and shutdown checks before
using it to evaluate an Apple TLS adaptation.

## 0056: Preserve the Apple platform register in LLVM context restoration

Status: signed Mac and native Linux checks, including the original negative control, pass at bba15cd.

The NDK unwinder's hand-written restore assembly loads saved x18 even when
compiled ART C++ reserves it. Reassemble the pinned upstream save/restore source
and first require exact text-section equality with both NDK archive members.
Adapt the paired x18/x19 load to load only x19 at its original offset, matching
the platform-register policy already used by ART's own long jump.

Test the actual routines using a poisoned saved x18, distinct x19/d8 contents,
and a captured stack/frame continuation. Keep a Linux-only original negative
control which restores live x18 before returning to its caller. Require the
adapted context checks on signed Mac code and native Linux, with an iOS 15
framework build. Preserve original source, notices, hashes and the rejected
unmodified behavior. This narrow prerequisite does not prove a complete unwinder
or add exception support to the guest ART runtime. See [the boundary](m3-unwind-context.md).

## 0057: Keep ART math inside the Android ABI

Status: signed Mac, Android-on-Linux and system-libm checks pass at 236625c.

The current Android ART link needs 20 math functions beyond the M2 libc subset.
Build their original pinned Bionic/ARM implementations as a separate signed
library. Retain AOSP's no-errno setting and Android binary128 compiler helpers;
do not bridge long double to Darwin or replace algorithms with approximation
stubs. Select only this dependency closure and expand it when callers demand
more entry points.

Use a separate NDK client with 78 fixed vectors before runtime integration.
Require the actual dependency group to execute through signed Mac wrappers and
the identical ELF libraries to pass on native Linux alongside system libm.
Preserve every original notice and source input. The fixture covers selected
results, not full libm accuracy, floating-point state or ART startup.

## 0058: Extend the existing Bionic source library for ART dependencies

Status: both source profiles, signed Mac modes, native Linux and iOS packaging
pass at `a3e46d6`; expanded syscall behavior remains outside this source closure.

Reuse Bionic's original implementations for the additional libc functions in
the ART dependency graph. Build the 50-unit closure with its pinned AOSP flags
and hash-check each new source. Keep the same guest TLS and syscall boundaries,
and retain the native/upstream instruction and symbol comparisons.

Exercise an original 30-case libc client in both signed allocator modes and
against native Linux libc. Add its result alongside the fixed M2 score so
neither new nor existing failures can disappear into a changed denominator.
Keep the iOS diagnostic runner shared with the Mac test. Compiled wrappers
whose kernel services remain unsupported stay explicitly unsupported; source
closure alone cannot establish directory, signal, property or process behavior.

## 0059: Resolve ART loader queries within the signed startup group

Status: portable queries and local tests pass; Android API integration pending.

Keep the initial ART dependency set fixed for the lifetime of the runtime.
Provide named dependency lookup, lookup after a caller, image enumeration and
address metadata in the portable load-group engine. Android names never reach
the host dynamic linker. This preserves the signing boundary without runtime
code mapping or a second dependency graph.

The first scope is one startup group. Explicit handles restrict lookup to their
dependency closure; the caller-based search traverses only the remainder of
the fixed group. Do not present these queries as general Android namespace,
unloading or TLS-symbol support. The forthcoming ABI bridge owns its error and
handle state and exposes valid guest metadata while the signed images remain
alive. See [the loader service contract](m3-loader-services.md).

## 0060: Pin loader images and separate handle state from each thread's errors

Status: native Linux API reference and portable context tests pass; signed
Android API bridge pending.

The startup group owns image lifetime. `dlopen` acquires a reference to an
already registered image, and `dlclose` balances that reference. Explicit
absolute guest-path aliases are copied from configuration; no basename fallback
or host search resolves an unknown path. Each context issues its own opaque
handle tokens and serializes reference changes. Metadata queries remain
immutable so program-header callbacks can reenter loader operations.

Keep the error record explicit and owned by the calling guest thread. The
platform bridge must validate guest strings, expose guest-readable error/name
storage and translate callback structures. Original AOSP libdl supplies the
entry points and caller-address capture. Native Linux establishes the 32-call
and six thread-error regression before those operations are connected to the
guest bridge. Unloading and dynamic namespaces remain outside the fixed
startup-group contract.

## 0061: Marshal loader metadata through the shared guest mapper

Status: portable tests, signed Mac API execution and iOS 15 framework packaging
pass at `10eb3b6`; full Apple ART startup remains pending.

Keep AOSP's original libdl frontend and implement its seven explicit loader
imports behind a thin native binding. Use host TLS to select the guest thread's
error state. Android library names and handles never reach the host linker.

Copy names to read-only guest pages and point program headers and symbol names
into the verified signed image. Encode Android LP64 callback structures
explicitly; host structure layouts and transient host stacks are not the guest
ABI. The mapper validates input strings and output storage. Callbacks run only
from declared signed executable segments and outside mapper locks, permitting
reentry. A TLS callback reports an existing block without allocating one.

The native acceptance runner owns guarded guest stacks until pthread join.
Compare its unchanged Android fixture with the native Linux 32-case and six
thread-error reference. Preserve source, object, ELF, signing and notice evidence
for both Mac execution and iOS 15 packaging. Physical execution remains unverified.

## 0062: Make the full native guest ART object build reproducible

Status: all 462 units and the complete CI workflows pass at `44008be`; downloaded
source, object and TLS-adaptation evidence verify independently.

Add an explicit Android managed-window guest profile to the shared Python
builder. Apply the existing hash-checked Bionic TLS header boundary before
compilation, and retain every changed header with the original notices. Keep
the VM implementation host-owned by excluding its two units from the guest.

For three pinned HeapSampler units, require the compiler's original assembly to
reproduce its object before replacing the five reviewed thread-pointer reads.
Verify source hashes, exact descriptor symbols/counts and the adapted instruction
inventory. Unrecognized accesses require investigation, never a broader rewrite.
The existing real Linux ART thread-state regression remains the behavior
reference. A build does not establish signed full-runtime execution.

## 0063: Link the full guest from one verified dependency revision

Status: full-runtime linking, instruction checks and signed Mac/iOS 15 framework
layout validation pass in CI at `5b570af`. Full native constructor/pre-start JNI
execution also passes at `d43ceaf`; JavaVM startup and DEX remain incomplete.

Consume the full native guest and already exercised Bionic, math, libdl and LLVM
context artifacts from the same producer revision. Verify their original source
and object hashes before linking. Preserve the producer revision separately when
a local consumer links downloaded CI artifacts. Only eight explicit shared
VM/TLS imports may remain outside the guest dependency exports.

Link NDK C++ support inside the Android runtime; do not share host C++ objects or
substitute Darwin libc. Retain the complete NDK notice and dependency provenance.
Place the reviewed context-restore object before the unwind archive and reject
extraction of its original x18-restoring member. Inspect the final linked code,
including extracted archive members, before signing. Existing Mach-O wrappers
carry the immutable code and writable ELF data layout. Packaging alone does not
establish constructor execution, signal behavior, ART startup or DEX invocation.

## 0064: Query Bionic's existing static TLS for loader metadata

Status: signed Mac execution passes at `d43ceaf`: 14 query rounds across seven
threads in each allocator mode; downloaded source and binary evidence verify.

Keep private Bionic TLS layouts in the Android bootstrap. Expose a fixed-word
module-ID-to-address entry for the guest loader's metadata callback. The startup
group is immutable and all of its TLS is static, so a query needs no allocation,
module-table mutation or allocating `__tls_get_addr` call. Invalid IDs, unbound
threads and calls before bootstrap return zero. Do not claim dynamic module
support: that will need generation-aware DTV lookup and additional tests.

Compare both existing block addresses with Bionic's real resolver on the main
thread and six workers, before and after their TLS workload. Keep these checks
inside both existing signed normal and sampled Bionic runs, alongside the
original Linux TLS reference. Do not substitute a host TLS layout for Bionic's.

## 0065: Execute full ART bootstrap through the existing Bionic services

Status: 31 constructors, pre-start JNI, shared heap binding and cleanup pass on
native Mac ARM64 at `d43ceaf`; JavaVM startup and DEX remain incomplete.

Use the same VM, rooted filesystem, syscall translator, futex and pthread
manager as the Bionic acceptance runner. Register libc, ART, libm and libdl in
one signed group, provide ART's seven VM imports plus the existing TLS bridge,
and bind each executing thread to the guest loader before constructors.
ART loader calls must resolve through AOSP libdl, without the M2 fixture's
optional-library failure hooks. Derive the TLS module count from the group.

First test real constructors, pre-start JNI registration and shared heap
binding. Keep that result separate from JavaVM startup and DEX acceptance.
Fixed-word invocation APIs can cross the native boundary directly; variadic
JNI method calls must remain in an Android-compiled acceptance entry because
Apple's variadic ABI differs. This is a one-shot diagnostic, with the existing
fatal-error policy and no general unloading, not a production launcher API.

## 0066: Report an explicit virtual identity through Linux uname

Status: regression reproduced before implementation. Portable checks, native
Linux comparisons and the ART constructor query pass in CI at `d43ceaf`.

ART's cache-operation constructor requires uname, a Linux sysname and a parseable
kernel release. Encode the six 65-byte Linux fields in the portable syscall
layer. Use a fixed `artbox` hostname and `aarch64` guest machine, without copying
host structures or exposing host identity. Release `0.0.0-artbox` makes kernel
feature/version predicates false. Accept conservative fallback behavior rather
than advertise kernel features merely because the host has a high version.

Test layout, padding, unaligned writes and memory errors, and compare the wire
layout and error cases with actual Linux on both existing hosts and native
ARM64 CI. Continue to test JIT/code-cache absence independently: this identity
is not the mechanism that prevents runtime code generation.

## 0067: Detect an unsupported memory prerequisite without aborting ART

Status: regression reproduced before the edit; all 16 injected cases and full
signed guest constructor execution pass at `d43ceaf`.

ART probes `MREMAP_DONTUNMAP` during static initialization even when the selected
collector is semispace. Its probe assumes shared anonymous mmap is available
and aborts on ARTBox's `EOPNOTSUPP`. Keep uname conservative. Do not claim shared
mapping or userfaultfd support just to bypass this probe.

In the native guest profile only, let the hash-verified AOSP probe return false
when its mmap prerequisite reports `ENOSYS` or `EOPNOTSUPP`. Keep allocation
failures and cleanup assertions intact. This accepts the existing slower
semispace path and leaves the original Linux reference untouched. Implementing
real shared mappings remains an option when an actual workload requires them.

Compile the extracted original and adapted function with deterministic syscall
outcomes before building the guest. Check unsupported mappings, allocation
failures, remap failure, successful remap and both unmap failure paths, including
call ordering and arguments. These are control-flow tests, not Linux memory
semantics tests. The full signed constructor test remains required in CI.

## 0068: Package Android ICU with the existing guest C++ runtime

Status: local Android link and instruction inventory pass; public CI packaging
and the complete workflows pass at `d68dd3e`. Eight ICU test groups execute
through the signed Mac runtime at `ecf9000`; physical iOS execution is unverified.

Keep nativehelper, ICU common, internationalization, shim and JNI registration
as five ELF libraries wrapped in signed frameworks. Link their base/log/C++
imports to the same guest ART image that provides those symbols, avoiding
duplicate C++ runtime state or cross-ABI calls into Apple's C++ library. Validate
the actual dependency closure, not a union of every image's exports.

The 480 pinned source units retain their existing AOSP host data-loading path
and Android ABI. Package the pinned ICU data as data, and preserve all notices
and source archives. Require the ART and ICU producers to match one Git
revision. Assert absent TLS sections and reject kernel or reserved-register
instructions before using the existing 16 KiB signed wrapper pipeline.

This isolates signing and dependency failures before runtime initialization.
The cost is five additional signed frameworks and their startup relocations;
measure load/constructor time when the native ICU acceptance runner is wired.
Packaging alone does not satisfy that execution test or M3.

The execution test adds one original Android test image and reuses the shared
Bionic runner with ten reachable images. Install the rooted ICU environment
before constructors, keep its C++/variadic test calls inside Android code and
release ICU caches/data with the upstream cleanup entry before VM teardown.
Continue to run the existing four-image ART and M2 acceptance paths separately.

## 0069: Accept ICU's random-access hint without changing memory semantics

Status: both anonymous and file regression tests fail before implementation;
all 25 local host tests pass afterward. Native Linux ARM64 comparisons pass at
`ecf9000`, as does the unchanged ICU data loader through the signed Mac runtime.

The unchanged ICU data loader maps its pinned data read-only/shared and then
calls MADV_RANDOM. Rejecting that advice aborts initialization. Unlike DONTNEED,
it is a caching hint, so retain range/ownership validation and leave contents,
protections, file references and host read-ahead behavior unchanged.

Forwarding an equivalent hint to Darwin is an optional later optimization.
For now the cost is potentially unnecessary read-ahead; this does not promise
any performance improvement. Keep the existing DONTNEED implementation and
explicit errors for other unsupported advice. Compare alignment, page rounding,
zero length, overflow, inaccessible pages and holes against Linux, and use
injected file backing to detect accidental remapping or writeback.

## 0070: Select POSIX error strings in the Android OpenJDK utility

Status: original Android object fails strict linking at `__xpg_strerror_r`;
unchanged source with the POSIX declaration compiles and links locally. Runtime
regression passes in both native Linux ART dependency builds at `030b310`.
Android-built helper execution on Apple remains pending.

The Linux-Bionic build flags select a glibc-only alias in `jni_util_md.c` when
`_GNU_SOURCE` is defined. This source expects the POSIX integer-returning API.
Undefine that macro for this Android unit and use Bionic's existing `strerror_r`.
Retain the original Linux flags and upstream source. A new native dependency
test calls the actual exported helpers and checks truncation, untouched buffers,
terminators, returned lengths and errno preservation.

Adding a glibc alias would broaden the guest ABI for a build-configuration error.
Forwarding to Darwin or Bionic's GNU variant would also introduce a different
return-value contract. The selected build flag needs no extra code, host import,
runtime-generated instruction or entitlement.

## 0071: Preserve Bionic vfork state while rejecting process creation

Status: the original object fails the native instruction gate as expected.
Both 281-unit Bionic profiles and the linked diagnostic client build locally;
the adapted objects contain no forbidden instructions. At `3826391`, both signed
Mac modes pass all 30 checks, and native Linux passes 28 captured cases plus the
required failing mutation control. Both complete host and iOS workflows pass.

OpenJDK's native process helper imports vfork even when no process is launched.
Select the pinned AOSP frontend and replace only its TPIDR_EL0 read and kernel
entry with existing guest TLS/syscall calls. Unlike a kernel entry, a C call
may destroy x9/x10, which hold the thread pointer and cached PID/vfork bits.
Save those registers and the frame/return address; retain the original flags,
state restoration and errno branch. Raw clone remains unsupported with ENOSYS.

Calling Darwin vfork would give guest code control over a native process with
unimplemented exec/lifecycle semantics. A successful placeholder would lie to
the library. Explicit rejection keeps the import resolvable without promising
subprocess support or changing the no-code-generation policy.

The oracle uses the actual production vfork and errno objects, prefixed to
avoid host interposition. Inject errno-boundary, parent and simulated child
results across both memtag modes and cached-state patterns. Deliberately
clobber the two caller-saved registers, check arguments and guarded storage,
and require a copy missing the save/restore to fail. Signed Apple execution
also calls the real guest vfork and checks ENOSYS and unchanged getpid. No
test launches a child. The cost is two ordinary bridge calls and stack frames
for an unsupported operation; this is not a process-performance benchmark.

## 0072: Generate Android IDs and retain Bionic's resolver dependencies

Status: the new NDK callers fail strict linking against the preceding libc.
At `1647514`, all 75 new checks pass in both signed Mac modes, 45 common checks
pass on native Linux, and the 16 original generator tests pass. Complete CI is green.

Native libcore imports account, resolver, network/interface and file wrappers
outside the existing Bionic subset. Select the original pinned units and their
BSD dependencies instead of replacing the library APIs with host calls. Use
AOSP's fs_config_generator `aidarray` mode and its real Android ID header;
Bionic's empty host table would silently change guest users/groups. Preserve
the package license declarations, complete Apache terms and corresponding
inputs. Run the original generator tests on POSIX hosts and verify identical
generated bytes on all build platforms after normalizing stdout newlines to LF.
The generator and Android ID values remain unchanged.

The acceptance caller uses AI_NUMERICHOST/AI_NUMERICSERV and numeric getnameinfo
so DNS and network availability cannot explain a passing result. Android ID
and reentrant-storage tests run against signed Bionic; common string/address
tests also run against Linux libc. Keep existing differences in short-buffer
error constants explicit. The 75 new checks do not increase M2's denominator
or claim that the additional syscall frontends have Darwin implementations.

The added sources contribute two original constructors: grp_pwd's file-table
initialization and the resolver's pthread key. Require exactly five Bionic
constructors instead of the previous three. The single mandatory startup
expectation and 328-case denominator remain unchanged; tests reject omitted or
extra constructors. The loader independently requires every selected ELF
initializer to return. Historical three-constructor reports remain evidence
for their original source revision.

This source closure increases signed code and relocation size, including
resolver paths not yet usable without their services. Measure the resulting
artifact; do not infer network support or JavaVM startup from successful linking.

## 0073: Sign native class libraries before registering JNI

Status: all 17 native groups and 228 integer vectors pass signed Mac execution at c3fd5aa.

Link the 208 selected Android native class-library objects against ART, ICU,
Bionic, math and libdl from the same clean producer revision. Preserve the
original javacore export map: its JNI class cache must remain distinct from
OpenJDK's. Only OpenjdkJvm may retain the existing explicit Bionic TLS import;
every other strong import must resolve through its DT_NEEDED graph. Reject
compiler TLS and unreviewed kernel/thread-pointer/reserved-register instructions.

Use a narrow archive containing exactly three hash-pinned NDK compiler-rt
unsigned-128 division members. Keep them local to the consuming libraries and
carry the original LLVM exception notice. A Python arbitrary-precision divmod
oracle supplies 228 boundary and deterministic generated vectors, including
null-remainder output. Calls with 128-bit operands stay inside Android code;
the Apple boundary sees only the integer result.

Run the original native libcore fixture with the five Linux capability groups
excluded from the Apple variant; Linux continues requiring all 22 groups.
Apple must pass all 17 applicable groups, including the actual monitor worker's
join and cleanup. Load both JNI libraries and check their exports, but defer
JNI_OnLoad until JavaVM exists. Constructor and native dependency execution
alone cannot establish Java or APK support. The cost is six additional signed
frameworks plus their ELF data and notices; no executable mappings are created.

## 0074: Preserve open inode lifetime when unlinking a guest file

Status: 29 cases pass in both signed Mac modes and both native Linux profiles at c211a46.

The first signed class-library execution at `3505832` reaches JVM file cleanup
and fails because unlinkat returns ENOSYS. Implement rooted non-directory
unlink rather than removing that cleanup assertion. Reuse the existing pinned
parent-directory walk, ignore dirfd for absolute paths, and never follow the
last symlink. A terminal '/.' must be preserved during unlink resolution; the
read-path normalization would otherwise select its parent for deletion.

Unlink removes the pathname while open descriptors and mappings keep their
native backing references. Test a zero link count, continued reads through the
old descriptor, and recreation of the same name as a distinct file. The mock
filesystem therefore holds inodes through shared ownership. The native provider
maps Darwin's directory-unlink rejection to Linux EISDIR. It rejects other
special files; directory removal with AT_REMOVEDIR remains explicitly unsupported.
The virtual system, device and proc trees stay protected, and parent traversal
through '..' or a symlink remains rejected. Removing a final symlink removes
only that directory entry.

The same 29-case NDK object runs through both signed Bionic modes and original/
adapted Bionic syscall entries on native Linux ARM64. Portable tests additionally
check virtual-tree protection, absolute dirfd handling, symlink confinement and
unchanged outside-root guard contents. These cases have a separate result field;
M2's existing 328-case denominator remains unchanged.

## 0075: Keep unrelated JNI libraries outside javacore's dependency scope

Status: local link, regression checks and signed execution pass at c3fd5aa.

At `c211a46`, the native class-library fixture passes file cleanup and monitor
join, then finds JniConstants through javacore's handle. The original export map
correctly exports only JNI_OnLoad and JNI_OnUnload from javacore. The link step
retained every candidate base library as DT_NEEDED, including libicu_jni, whose
public class-cache initializer has the same name. Breadth-first handle lookup
therefore correctly finds that different library's symbol.

Use the linker's --as-needed selection for the five class libraries. Continue
requiring every strong import to resolve in its actual dependency graph and
reject JniConstants exposure anywhere in javacore's lookup scope. Tests cover
direct exports, direct and transitive dependency leaks, and unrelated preloaded
libraries. The test caller keeps its explicit full manifest so all 15 images
remain reachable. Preserve the original runtime visibility assertion and loader
lookup semantics. This changes only build-time dependency metadata; it adds no
runtime code-generation or platform entitlement requirement.

## 0076: Queue guest thread signals without signalling the host process

Status: portable tests, both signed Bionic modes and identical-object native Linux comparison verified.

ART's SignalCatcher waits for blocked SIGQUIT/SIGUSR1 and requires a
thread-targeted wakeup to shut down. Implement standard pending signals in the
portable process/thread namespace with mutex/condition-variable synchronization.
This avoids mapping a guest TID onto an unrelated host PID and works on the
Windows host as well as Apple platforms. A zero signal probes the target without
enqueueing; standard signals coalesce, and synchronous waits select the lowest
requested number. Preserve Linux error ordering and consumption before failed
siginfo copyout. The 33-case shared caller also runs on real Linux.

Attach before native thread start, inherit only the mask, and detach after
native join but before clear-TID publication. Roll back failed native starts and
reject process destruction with attached threads. Cross-thread mask access goes
through the queue's synchronization, not the kernel descriptor's owner-only field.

Do not return success for unblocked/default delivery, SIGKILL/SIGSTOP or realtime
signals before those paths exist. Unblocking a pending signal fails explicitly
without mutating the mask. No host signal handlers are installed by this slice;
signal registration and alternate stacks remain open. The mutex path cannot
serve ART's mask calls from an asynchronous handler: that requires a separate
signal-safe bridge with Linux context conversion. Do not disable ART sigchain.

## 0077: Translate handler contexts as data and preserve the platform register

Status: portable codec and native Linux/Darwin context return verified; guest handler integration pending.

ART's ARM64 fault handlers read and rewrite Linux ucontext PC, SP and registers.
Darwin's context cannot be passed directly to them. Define an original portable
byte codec from the pinned Bionic ARM64 UAPI layout and test it against both
Bionic headers and native Linux signal frames before wiring host delivery.
The codec uses caller-owned storage, no allocation, locks, syscalls or loader
lookups. It does not generate instructions or emulate their execution.

On resume, accept the emitted FPSIMD/ESR record layout and guest changes to
ordinary registers and NZCV. Preserve interrupted x18 and other PSTATE bits:
Android code is already compiled with x18 reserved, and Apple owns that register.
Unknown extensions fail explicitly; a future SVE/SME adapter needs its own
state contract. Fault address and ESR remain observations of the interruption.
Check PC/SP alignment here; signed-code/stack ranges and Darwin context access
belong to the platform delivery adapter. Do not treat encoding alone as handler
delivery or as a successful sigaction implementation.

The native Linux test deliberately executes a precompiled BRK, transforms its
real kernel context through the codec and resumes at the following instruction
with changed x0/v0. A second run drops those edits and must fail. This tests
actual kernel signal return without implying Apple handler integration.

The Darwin adapter uses public SDK mcontext fields and thread-state pointer
accessors, reviewed against the [iOS 15-era XNU declarations](https://github.com/apple-oss-distributions/xnu/blob/xnu-8019.41.5/osfmk/mach/arm/_structs.h).
No Apple source is copied or vendored. Keep capture/apply independent of signal
number, mask and siginfo translation so no Darwin encoding leaks into the guest.
The live host x18 is preserved even if a caller bypasses the portable decoder.
The initial adapter targets arm64, with a separate arm64e contract required
before claiming authenticated-context support. Test actual Darwin BRK return
and dropped edits on Mac; ordinary iOS 15 compilation is a separate check.

## 0078: Bind a separate syscall and TLS scope before guest signal execution

Status: verified on native ARM64 Mac at 9200f91; complete CI passes.

A signal can interrupt code while the VM mapper or stdio holds a lock. Reusing
the ordinary dispatcher from ART sigchain would reenter those locks. Likewise,
the first access to compiler TLS may require initialization. Create one public
pthread key in ordinary context and explicitly attach each participating thread
before delivery. Retain the key for the library lifetime to avoid key-reuse
races; free per-thread storage on explicit detach after all scopes have left.

The handler path reads that initialized key and publishes a borrowed immutable
scope with lock-free pointer atomics. It never invokes pthread_once, key creation,
allocation or cleanup. Public pthread_getspecific was reviewed in the
[iOS 15-era libpthread implementation](https://github.com/apple-oss-distributions/libpthread/blob/libpthread-454.60.1/src/pthread_tsd.c),
where it delegates to a direct lookup. This is an Apple implementation contract,
not a portable claim that every pthread implementation is signal-safe. No
private TSD keys, offsets or APIs are used and no Apple code is vendored.

The Bionic syscall and TLS endpoints check the signal scope first. Keep ordinary
compiler-TLS access behind non-inlined helpers; a volatile TLS read prevents
speculative lookup on the getter path. A scope provides a separate fixed-word
dispatcher and initialized guest TLS pointer, preserves host errno, rejects
TLS replacement, and restores the interrupted binding on exit. Normal calls
pay an additional scope lookup; measure this overhead with the eventual runtime.

The Darwin BRK test now faults from inside artbox_vm_transfer while its mapper
lock is held. Its ordinary dispatcher would reacquire that lock; the handler
must use its separate scope and resume normally. Tests also cover nested scope
restoration, per-thread isolation, active-detach rejection and cleanup. This
still does not install guest actions or implement handler-time Linux mask calls;
it establishes the binding required to do so without reentering locked services.

## 0079: Publish immutable actions and first deliver a signed Android trap handler

Status: verified at 2aa064f; both signed Bionic modes, native Linux and complete CI pass.

Keep Linux rt_sigaction copyin/out and ownership in the portable process service.
Enable registration only with a platform capability validator, before threads
start. Publish complete immutable records through a lock-free atomic pointer;
handlers snapshot or reset without taking the ordinary writer mutex. Retain
records until process destruction so an interrupted reader cannot observe freed
memory. Bounded exhaustion returns ENOMEM without changing state; the diagnostic
owner reserves 4,096 records (128 KiB). This trades bounded lifetime storage for
simple safe publication; reclamation needs a separate quiescence protocol.

Use a real NDK-compiled handler registered through Bionic sigaction as the next
integration test. Begin with BRK/SIGTRAP, SA_SIGINFO and optional SA_RESTART on an
attached guest stack. Snapshot the logical mask atomically, construct Linux
siginfo/ucontext, enter the signal syscall/TLS scope and invoke signed code
through the existing fixed-word ARM64 boundary. Validate the resumed PC against
stable signed RX ranges and SP against the owned stack before touching Darwin
state. No allocation, VM lookup, mutable loader query or stdio occurs in delivery.

The initial signal dispatcher supports immutable getpid/gettid and a mask query
into the attached stack. It rejects mask mutation, alternate stacks, changed
return masks, custom restorers and unsupported action flags rather than passing
Darwin layouts or semantics into Android. Process-wide host disposition changes
belong to the execution owner, which restores SIGTRAP after guest threads stop.
Fatal/default routing and other faults remain unfinished; this is not yet ART's
full sigchain boundary. Ordinary masks and blocked queues retain their tests.

The caller checks registration/error ordering, real Bionic errno/TLS, Linux
TRAP_BRKPT data and PC/x0/SIMD resume. Dropping register edits must fail while
still advancing BRK. Run the same source with Linux libc as an independent
reference; its sigaction wrapper layout differs, so do not call this an
identical-object oracle. Do not change M2's fixed acceptance denominator.

## 0080: Separate host conversion storage from the guest signal stack

Status: verified at e7e1647; both signed Bionic modes, Linux and complete CI pass.

Install a guarded host alternate stack before guest execution on each Apple
thread, preserving any earlier host registration. The execution owner requests
SA_ONSTACK for its host trap callback. This keeps the host context adapter off
an exhausted guest stack. An AOT ARM64 function switches to the selected guest
stack for the callback and restores the host SP afterward; no executable memory
is created at runtime. Preserve both ABIs' callee-saved registers and Apple's x18.

Place Linux siginfo followed by ucontext on the guest-selected stack, above the
callback's SP. SA_ONSTACK selects the registered alternate stack when needed;
without that flag, use the interrupted stack even if alternate storage exists.
Already-active alternate stacks continue downward. Compute guest SS_ONSTACK from
the actual guest SP, independently of the host's private-stack state. Return
requires validated code/SP, an unchanged alternate-stack description and the
ordinary signal-context checks. Nonlocal exits from handlers remain unsupported.

The portable process service publishes immutable alternate-stack records and
frees them after the owning thread stops. Queries never allocate or lock.
Updates require owned writable storage; SS_DISABLE needs no record. Shared-VM
clone starts disabled, while Bionic may subsequently install its own stack.
Follow Linux input/copyout ordering, EPERM on an active stack and SS_ONSTACK
input acceptance; SS_AUTODISARM needs a separate return contract and is rejected.

The virtual ARM64 ABI advertises an 8 KiB minimum in AT_MINSIGSTKSZ, rather than
copying a host kernel's size. This budgets the 4,688-byte Linux frame, alignment,
red-zone preservation and bridge call depth. The earlier native Linux reference
measured 5,120 bytes; the stricter ARTBox minimum is a documented translation
cost. ART's pinned 32 KiB allocation meets it. Each attached thread additionally
reserves at least 128 KiB for host conversion plus two native guard pages. These
pages are RW only and are unmapped after restoring the prior host registration.

Validate with 17 shared wire cases and 24 same-source handler/worker checks on
native Linux and signed Bionic. Omitting SA_ONSTACK must fail the stack-location
assertion. The extra signal worker is reported separately from M2's six-worker
group; the fixed 328-expectation acceptance denominator remains unchanged.
The native caller also places SP 512 bytes above the normal stack's lower bound
for a deliberate BRK, then restores it after alternate-stack delivery returns.

## 0081: Publish signal masks and pending bits as one transition

Status: native Linux and both signed Bionic modes verified at `e8408fd`; all CI green.

A mask kept only in the handler's local scope hides it from other guest threads.
Separate atomic mask/pending words also permit an enqueue accepted under an old
mask to become stranded by a concurrent unblock. Use one 128-bit state containing
both words. Enqueue, wait consumption and mask changes compare/exchange that
state; standard signals still coalesce. A mask transition that would expose
pending signals returns ENOTSUP without mutation until unblocked delivery exists.
Never silently drop a pending signal on handler return.

On ARM64 Clang targets, require always-lock-free, aligned 16-byte operations and
disable outlined atomics for the signal implementation. The handler path must
contain native exclusive-pair loops, not library locks or runtime dispatch.
Other targets use an ordinary-context mutex and a separately published lock-free
query, and explicitly reject handler-time mutation. This preserves portable
host functionality without claiming asynchronous guarantees that its compiler
has not provided. The lock-free loop can retry under contention; it is not a
wait-free operation. No executable allocation or private platform API is used.

Publish the action mask plus the delivered signal before calling Android code.
Handler BLOCK/UNBLOCK/SETMASK operates on the shared state, preserving Linux
unmaskable bits and mutation-before-copyout ordering. The saved ucontext mask
remains independent and controls restoration; validated guest edits are accepted
when they do not require an unsupported pending-signal delivery.

ART sigchain passes masks from image globals as well as stack locals. Snapshot
stable signed RX and owned RW image ranges before delivery, allowing reads from
both and writes only to RW storage or attached stacks. Do not consult the VM
mapper in a handler, and do not expand SP validation to ordinary image data.
Dynamic heap buffers remain outside this fast copy contract.

Test 4,096 enqueue/unmask races and mapper-lock-held mask calls, with positive
handler capability required on Apple ARM64. The same-source Linux/Bionic caller
checks 18 handler assertions, RO input/RW output, copyout failure after mutation,
a worker's queued SIGUSR2 and an edited return mask. Omitting UNBLOCK must fail.
The two worker instances are counted separately from the fixed M2 denominator.
Action flag probing, other fault transports and actual ART sigchain execution
remain separate acceptance work; this change does not establish Apple JavaVM.

## 0082: Probe delivery capabilities and exercise the actual AOSP signal chain

Status: 22 real sigchain checks and removal control verified on native Linux and
signed ARM64 Mac at `886d1bb`; all CI green.

Pinned ART installs its handler with SA_UNSUPPORTED and SA_EXPOSE_TAGBITS, then
reads back the accepted flags. Give the delivery owner an explicit supported
flag set. When the probe bit is present, intersect the requested flags with
that set before validating the handler and publishing the immutable action.
Never advertise the probe bit. Without it, unsupported flags still fail with
ENOTSUP. This deliberately differs from Linux's unconditional unknown-bit
clearing: callers that require unsupported semantics must not silently succeed.
Copyin, validation and publication-before-copyout ordering remain unchanged.
The initial Apple owner advertises SIGINFO, ONSTACK and RESTART; it does not
claim tagged fault-address or other fault-transport support.

Compile an original caller against the existing, unchanged AOSP sigchain header
and implementation. Keep it in a separate native-guest fixture unit (463 objects
in that profile). Pass named libart sigaction/sigprocmask addresses into the
caller, because libc occurs earlier in the load group and has symbols with the
same names. Warm sigchain's own pthread key through its wrapper before faults;
first-handler allocation is not an acceptable shortcut.

The caller checks special-handler acceptance, fallback to a user handler, handler
removal, action readback, alternate-stack execution, return masks and scoped TLS
mask behavior. Removing the special handler before execution is a negative
control. Run the same caller with the unchanged pinned source and Linux libc as
a reference; do not claim identical Bionic machine code. AOSP retains a claimed
chain after the last special handler is removed, so restore its user disposition
and let process teardown own the kernel registration. This tests sigchain, not
JavaVM startup, JNI_OnLoad or DEX execution. No code generation or new entitlement
is involved.

The two libc profiles have different reserved-signal policies. Pinned Bionic's
`filter_reserved_signals` always blocks `BIONIC_SIGNAL_POSIX_TIMERS` (signal 32)
in sigprocmask; glibc does not add that bit. The caller must assert the full mask
including Bionic's required bit, using the pinned header's constant. Do not
discard reserved bits from observations or change Bionic's behavior to fit a
glibc expectation. Per-assertion failure bits and mask snapshots are read and
formatted only after the handler returns.

## 0083: Measure native fault metadata before extending ART signal delivery

Status: five native cases and both mutation controls verified on Linux and signed
ARM64 Mac at `f6251fd`; all seventeen host jobs and the iOS workflow pass.

ART's fault manager needs real SIGSEGV delivery. A Darwin signal number alone
does not establish the corresponding Linux fault classification. First measure
the public siginfo and ARM64 exception state for null access, protected reads,
read-only writes, misaligned exclusive loads and an undefined instruction.
Execute real faulting instructions in signed/native host code and resume through
the public signal context; do not allocate executable memory or emulate them.

Linux reference assertions require MAPERR/ACCERR, BUS_ADRALN and ILL_ILLOPC where
specified by each case. The Darwin reference records its actual signal/code and
ESR, accepting SIGSEGV or SIGBUS for the data faults during characterization.
Both must preserve fault addresses, use a guarded alternate stack, resume edited
PC/x0 and preserve x18 and errno. Dropping return edits or fault-address metadata
must fail. Diagnostic formatting occurs after return, never inside the handler.
These measurements are prerequisites for translation, not proof of Android
SIGSEGV transport or Apple JavaVM startup.

The first reference run (`7a70c04`) reached the intended LDXR instruction but
did not fault at byte offset 1 on either ARM64 host. FEAT_LSE2 permits exclusive
accesses contained within one aligned 16-byte quantity; see the
[Arm maintainer's architecture explanation](https://lists.infradead.org/pipermail/linux-arm-kernel/2024-September/959501.html).
Use an eight-byte access at byte offset 9 to cross that boundary while remaining
inside writable mapped memory. Keep the required alignment-fault assertion;
accepting a nonfaulting execution would invalidate this reference.

At `549b221`, Linux passes the full reference and Darwin passes all five fault
observations before failing alternate-stack cleanup. Darwin validates the size
even when restoring SS_DISABLE. Normalize that unused size to MINSIGSTKSZ, as
the existing delivery owner already does, and query the resulting registration
to verify restoration before releasing the test's alternate-stack storage.

## 0084: Classify synchronous Android faults using nonblocking VM metadata

Status: verified at `dbb7b1b`; both signed Bionic modes, native Linux comparison,
all 17 host jobs and the iOS build pass. Apple JavaVM remains pending.

Measured Darwin SIGBUS covers both access violations and alignment faults. The
ARM64 syndrome also reports a translation fault for a mapped PROT_NONE page.
Use Linux mapping ownership and protections alongside the syndrome; copying
Darwin's signal number or classifying from ESR alone produces incorrect signals.
Keep this classification in portable C, with native signal/context validation
and delivery in the existing Apple owner. BRK, UDF, data translation/permission
faults and unaligned exclusive accesses have actual native reference cases.
Do not classify an accessible file mapping's fault as SEGV; file EOF/I/O,
external aborts, MTE and instruction fetch faults need separate contracts.

A handler cannot take the VM mutex, and a published snapshot cannot point into
vectors that another thread may resize or free. An immutable page mirror would
require retention/reclamation and duplicate potentially large managed-window
metadata. Use a lock-free reader counter and writer gate instead. Serialized
writers announce mutation before waiting for existing readers to finish; new
readers immediately return EAGAIN. Sequentially consistent operations make the
gate/counter handshake coherent. Readers do bounded metadata work, never wait
for the interrupted writer and release their count before returning. GCC and
Clang ARM64 builds disable outlined atomic helpers. VM destruction requires
quiescent readers, as it already requires stopped guest access.

This adds two atomic words per VM and atomic operations around mapping changes
and fault queries, without a per-page mirror. The cost is explicit failure of
fault translation during a concurrent mapping change, even on another range.
The owner takes its failure path without editing the host context in that case.
This is a documented compatibility limit to improve before general concurrent
AOT fault handling, not an approximation of a successful Linux delivery.

Test mapper-mutex-held reads, mutation-callback rejection, poisoned-state output
stability, file/anonymous replacement, borrowed boundaries, managed-window holes
and 1,024 structural/page races. The real Apple BRK handler also reads metadata
while its interrupted callback owns the mapper mutex. The Android/Linux caller
then exercises five real faults with Linux codes, addresses, alternate-stack
selection, masks, TLS/errno and edited register return. Two mutations must fail.
Keep M2's denominator unchanged; these are separate M3 boundary checks. Neither
the reference nor the new bridge establishes JavaVM/JNI/DEX startup on Apple.

## 0085: Enter the real JavaVM through a signed Android JNI caller

Status: full signed Mac JavaVM/hello/GC/exception/attachment/shutdown acceptance
passes at `68400fe`, including the missing-hello control. Earlier stack, signal,
cwd and requeue failures are addressed by ADRs 0086-0089. iOS embedding is next.

The signed dependency group already runs ART, ICU and libcore constructors and
native checks. Add a separate runtime entry using that same group and the
verified implementation DEX/managed fixture from CI. All JNI varargs and C++
runtime APIs stay inside an NDK-compiled helper within libart. The Apple boundary
passes fixed-width arguments and receives timing/memory observations. Keep
the existing native-only and pre-start checks in their own processes.

Require actual `JNI_CreateJavaVM`, the exact hello string, allocation/GC,
exceptions, four native attachment cycles and successful VM destruction.
Require the switch interpreter with no JIT or profiling cache before and after
execution. Runtime executable mappings remain denied by the shared mapper;
the existing compiler factory fails if invoked. A separate missing-hello process
must start the VM and then fail class lookup. A build, constructor result or
startup log alone cannot satisfy this acceptance.

Select AOSP's unchanged `runtime_android.cc` in the native guest profile, as
the pinned runtime Android.bp does for Android, and expose `/system` as the
logical ANDROID_ROOT. This omits AOSP's Linux-host crash-dumper registration
on the normal Android path; Runtime::Init still initializes the real fault
manager and sigchain separately. Using the host crash-dumper would additionally
require unimplemented fatal/timeout signal behavior before normal startup.
Do not fake those registrations or use NoSigChain. Existing host monitor and
thread units remain selected to preserve the tested pthread subset without
adding Android kernel/HAL dependencies. Native Linux references keep their
host platform unit and crash diagnostics.

This initial execution uses the slower C++ interpreter. It proves neither
OAT execution nor the temporary eight-byte stack alignment used by ARM64 AOT
null-fault stubs. iOS embedding and managed execution still need their own
signed artifact verification before M3 can be completed.

## 0086: Own the JavaVM lifecycle on an explicit Bionic pthread

Status: signed worker entry and stack checks pass at `fc2f939`; JavaVM startup
then fails Linux signal 34 registration in native class-library initialization.

The first actual Apple invocation at `7d93946` enters ART initialization and
fails in `Thread::GetThreadStack`: Bionic's primordial-thread attributes need
`getrlimit(RLIMIT_STACK)`, then `/proc/self/stat` and `/proc/self/maps`. Those
interfaces are outside the current kernel subset. The fatal path then hangs
until the required acceptance timeout; this is not a successful VM startup.

Create a real Bionic pthread with a requested 4 MiB stack and run creation,
managed calls and destruction on it. This follows the JNI specification's
[recommendation to create the VM on a new thread](https://docs.oracle.com/en/java/javase/24/docs/specs/jni/invocation.html#creating-the-vm).
Before invoking ART, require a non-primordial guest TID, successful real
`pthread_getattr_np`, and a local address inside the reported usable stack.
Bionic includes its guard and excludes internal thread storage from reported
stack bounds, so record the requested size, actual bounds size and guard
separately. Join before releasing the worker's arguments or heap owner.

Implementing complete initial-thread resource/proc metadata is an alternative,
but would expand the kernel contract without benefiting this embedded VM's
normal worker lifecycle. Keep those calls unsupported and do not synthesize
successful replies. This choice uses the existing clone/pthread/TLS bridge,
adds one native worker and its stack reservation, and requires no code
generation. It does not resolve ART's other signal, syscall or shutdown needs.
Keep the hello, GC, exception, attachment, policy, destruction and negative
lookup checks mandatory. Failure artifacts record the phases actually reached;
the overall result stays failed until the entire acceptance passes.

## 0087: Deliver Bionic's queued interruption through a public pthread signal

Status: verified at `06fa115` in both signed Bionic modes, the native Linux
reference and the comparison using the identical NDK queue object. JavaVM/DEX
acceptance still fails later in startup; the other 16 host jobs pass.

At `fc2f939`, the signed JavaVM worker reports a requested 4 MiB stack, 4,214,000
usable-attribute bytes and a 16 KiB guard. ART advances into native class-library
initialization. Unchanged AOSP AsynchronousCloseMonitor and NativeThread install
a one-argument, non-restarting handler for Linux signal 34. Rejected registration
makes NativeThread throw IOException and ART abort. Deferring that registration
would only postpone a real I/O interruption requirement; implement delivery.

Configure one realtime thread-interruption number before attaching threads.
For the supported single-process tgkill API, all pending records have the same
SI_TKILL, PID and UID, so a bounded count retains every accepted send. Pack it
beside the standard pending bits in the existing atomic mask/pending state.
Mask changes and consumption then share one CAS: a signal racing with unmask
cannot disappear. Capacity exhaustion returns EAGAIN; other realtime signals,
process-directed queues and arbitrary siginfo need separate implementations.
The behavior reference is [Linux v6.12 signal.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/kernel/signal.c);
no kernel code is incorporated.

Use public pthread_kill with Darwin SIGUSR1 as a coalescing wake for that count.
The owner reserves SIGUSR1 during guest execution, installs SA_SIGINFO|SA_ONSTACK
without SA_RESTART and binds each native pthread before exposing it to guest
execution. The registry lock protects send-versus-unbind lifetime. Teardown
blocks and unbinds the transport, drains a pending wake and restores the prior
host mask and alternate stack before releasing storage. Error paths retain
live storage if restoration fails. Notification must remain signal-safe; see
the [POSIX function list](https://man7.org/linux/man-pages/man7/signal-safety.7.html).

The native callback may interrupt guest instructions or ordinary host syscall
code on an attached stack. Invoke the signed one-argument Android handler below
that interrupted SP while preserving Darwin's red zone; keep host registers
unchanged. The existing separate TLS/syscall scope supports only its tested
signal-safe endpoints. Apply the action/self mask, consume one pending count,
record the delivery epoch and restore the original mask after callback return.
Drain at most 64 callbacks per native entry, scheduling another wake for any
unblocked remainder. Synchronous fault handlers block the transport while their
scope is active. Async SA_SIGINFO, SA_RESTART, SIG_IGN, nonlocal exits and nested
fault handling remain unsupported; default delivery is fatal in this owner.

Host condition variables cannot be notified safely from a signal callback.
The ordinary futex and sigtimedwait paths compare the delivery epoch at intervals
of at most 5 ms and return EINTR for this non-restarting handler. This explicitly
adds up to 200 timed wakeups per second per waiting guest thread and up to one
poll interval of interruption latency, plus scheduling delays. It requires no
generated code, private APIs or executable writable memory. Direct host I/O can
use the native non-restarting signal; each additional syscall needs its own test.

Keep the original 328-case M2 score unchanged. Add 26 raw queue cases, quota and
lifetime tests, 4,096 concurrent accepted deliveries and interrupted-wait tests.
Native Linux and signed Bionic additionally require queued handler delivery,
mask/TLS/errno/stack observations and a real pthread futex interruption. Dropping
one queued send must fail the expected observation. The Linux oracle reuses the
identical raw-ABI NDK object and compiles the handler from the same source, since
the libc sigaction declarations differ. None of these tests substitutes for the
required full JavaVM, hello DEX and shutdown acceptance.

## 0088: Report the existing virtual cwd through the Linux getcwd ABI

Status: all 22 cases pass both signed Bionic modes and both native Linux syscall
profiles at `a9e91b6`, with the identical NDK object verified independently.
ART advances to Signal Catcher creation, then fails private futex requeue.

At `06fa115`, ART passes signal-34 registration and advances through native
initialization. The last recorded unsupported call is getcwd (17), followed by
the 60-second acceptance timeout. The pinned libcore System.c calls getcwd in
System_specialProperties and passes its result directly to strncat. A null
result is a concrete missing prerequisite; the log does not establish the full
subsequent failure path. Do not work around it by changing class initialization.

The VFS already resolves relative names from its virtual `/` and has no chdir
support. Implement getcwd there, returning that path and its terminating NUL.
Keep the host backing directory private. Match the unsigned-long size argument,
ERANGE-before-copy ordering, actual-byte copy length and return count described
by [Linux v6.12 d_path.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/fs/d_path.c).
Invalid or unwritable output returns EFAULT; output contents after a failed
copy are unspecified. Mutable cwd and directory rename/deletion need future
contracts. This call copies two bytes, adds no allocation and needs no code
generation or host filesystem query.

The original shared fixture covers zero/short capacity, invalid pointers,
unaligned output, canaries, full-width sizes, protected pages and a boundary
whose declared capacity exceeds its mapping. Run its identical NDK object
through both signed Bionic profiles and both Bionic syscall-entry profiles on
native Linux, with the Linux test child at `/`. Preserve M2's 328-case score
and require the complete JavaVM/DEX acceptance after this fix.

## 0089: Requeue private futex waiters for ART condition variables

Status: verified at `68400fe`: 18 cases in both signed Bionic modes and both
native Linux syscall profiles, real waiter movement against the Linux kernel,
and full signed Mac JavaVM/DEX lifecycle pass. All 17 host jobs and iOS CI pass.

At `a9e91b6`, the signed ART worker reaches Signal Catcher creation, then exits
with ENOSYS from syscall 98, operation 131. Pinned AOSP ConditionVariable::
RequeueWaiters uses FUTEX_REQUEUE_PRIVATE to move condition-variable waiters
onto their guard mutex. Implement the queue operation without changing that
upstream synchronization algorithm or pretending the operation succeeded.

Interpret the fourth syscall argument as a signed 32-bit move count. Reject
negative wake/move counts before checking source then destination key alignment
and user range. Private keys need not have mapped words. Wake the requested
prefix and move up to the remaining budget under the same lock that serializes
wait registration and wake. Return woken plus moved, including same-key moves.
Preserve each waiter's bitset, original deadline, native condition variable and
signal-delivery owner. The zero-wake case must not inherit the separate WAKE
opcode's historical nonpositive-count quirk. See [Linux v6.12 requeue.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/kernel/futex/requeue.c)
for ABI behavior; no Linux implementation code is copied.

This adds a linear scan of the bounded active queue (at most 65,536 entries),
without allocation, code generation or a new platform API. Shared requeue,
comparison, PI and robust owner recovery remain unsupported. Returning to ART's
mutex wait can add native scheduling overhead; no steady-state benchmark is
claimed from the correctness fixture.

Keep an 18-case original NDK wire caller separate from M2's frozen score. Require
its identical object in both signed Bionic modes and both native Linux syscall
profiles. In C++ tests, move real blocked waiters, check destination bitsets,
private/shared isolation, bounded wake/move counts, retained timeouts and 128
registration/requeue/wake races. Linux builds repeat these against the kernel.
The interruption test delivers signal 34 after moving the waiter and requires
EINTR. Required full JavaVM, hello DEX and shutdown acceptance stays enabled;
these narrower tests do not complete M3.

## 0090: Embed the verified ART group and expose actual guest stdout

Status: signed console acceptance, both controls and integrated ART IPA pass
at `c7807da`; all 18 host jobs and iOS CI pass. Downloaded bytes verify 16 signed
iOS images, 15 ELF pairs, five runtime resources and 175 notices.

The complete signed ARM64 Mac runtime passes at `68400fe`. Package that same
15-library ELF group, boot DEX, hello/managed DEX and ICU data in the iOS app.
Keep a separate build from the M1/M2 diagnostics because the current native
runner owns one process-wide lifetime. Do not imply reusable VM creation or
general APK launching. Continue using the project's independent console shell;
launcher UI remains deferred.

A cross-platform Python staging step requires exact producer revisions, full
managed acceptance and the missing-class control. Validate both Apple framework
layouts against the original ELF, the executed Mac hashes, empty entitlements,
notices and runtime data before copying. Recheck the embedded bytes after Xcode
builds the app. A required CI job follows signed acceptance and both Linux ART
references, signs an ordinary arm64 iOS 15 transport IPA and preserves provenance.
No writable executable mapping, runtime compiler or new entitlement is added.

Give the shared native entry an optional stdout callback separate from its JSON
result. Ordinary guest writes already copy at most 1,024 validated bytes; mirror
successful stdout writes to this callback. Signal scopes use their existing
separate dispatcher. The sink is immutable during execution, accepts chunks on
guest workers and remains alive through join and shutdown. The native acceptance
collector requires the actual hello and lifecycle text from that path. UIKit
copies the chunks and dispatches display updates to its main queue.
A separate process drops every console chunk and must fail the console check
after successful managed execution and cleanup, proving that VM success alone
cannot satisfy the log requirement.

Stage immutable resources in the bundle and copy data into an owned per-run app
container root. Signed native code stays in frameworks. The extra disk copies
and imageless interpreter startup are explicit initial costs; no iPhone memory
or startup measurement is inferred from Mac tests. Physical checks remain waived
and unperformed. General AOT/OAT packaging, repeated runs and process isolation
need separate contracts.

## 0091: Establish Binder's ARM64 wire boundary before service integration

Status: boundary implemented; local portable and NDK comparisons pass.
Driver delivery and real servicemanager remain pending M4 contracts.

Use Android 15's 64-bit Linux Binder ABI, retaining the AOSP tag already used
by Bionic/ART. Pin only the 15 reference files needed to inspect libbinder,
servicemanager, AIDL and licenses. Its Android.bp disables Darwin and its main
loop uses Binder polling and timerfd; an upstream host build is not a ready-made
Darwin runtime. Keep real ServiceManager registration/death behavior as the
integration target and review its libutils/libbase/SELinux/VINTF dependencies
before selecting additional code. Do not substitute fakeservicemanager.

Start with a portable, allocation-free command/snapshot validator. Match full
Linux ioctl words rather than host `_IO*` macros or arbitrary encoded lengths.
Retain full-width addresses without dereferencing them. Decode the packed
12-byte death payload, distinguish transaction extensions and validate object
offset ordering, bounds and four-byte alignment. Test actual NDK-produced bytes
and constants independently of the parser's C layout. Recognizing a command
does not enable its semantics, and the parser's errors are not syscall errno.
The eventual driver must define consumption/error ordering separately.

The planned driver owns an endpoint per Binder open, including separate handle
tables and receive arenas, with thread-specific synchronous call stacks. Linux
creates a binder_proc on each open even when the host PID is the same; this
permits a raw native test client to reach the real service manager in the first
single-process runtime. A single libbinder ProcessState cannot be used to prove
cross-endpoint delivery by invoking a local BBinder object. Model close/death
at endpoint lifetime; distinct Android application processes remain future work.

Use a bounded portable queue for owned transaction state and a thin Unix socket
notification provider for Apple/Linux polling. Windows host tests can inject a
provider. Socket readiness is a wakeup; the driver must retain queue/arena
ownership until delivery and explicit release. This plan still needs concurrency
and native Linux reference tests before it becomes a supported transport.
Mach ports would make early iteration and Linux comparison less portable.

Copying parcels and serializing shared driver state cost CPU and memory; measure
those costs after a real ping/pong path exists. No dynamic executable memory,
JIT, entitlement or guest kernel is needed. The Apple signed-library packaging
and source-level syscall boundary from M3 remain the execution model. See the
[Binder contract](binder.md) for completed checks and outstanding behavior.

## 0092: Make receive-buffer ownership explicit before exposing Binder mmap

Status: portable arena/concurrent lifecycle tests and native Mac/Linux alias
mapping checks pass; driver delivery and allocator comparison remain pending.

Give the driver a bounded metadata vector and caller-owned receive storage.
Allocation produces a reserved buffer; only this state permits driver writes
or cancellation. Publication ends mutation and permits guest release. Exact
allocation starts identify buffers. This is an internal API: its EPERM/EINVAL
results are not automatically the result of BC_FREE_BUFFER, which needs its
own Linux command-consumption contract. Driver serialization must cover copying
the return command and publishing its buffer before a guest release can run.

Round data/extra-buffer storage to eight bytes, require eight-byte offset table
entries and reserve at least eight bytes for empty transactions. Zero reused
extents including padding and honor clear-on-free. Admission fails atomically
on overflow, fragmented capacity or exhausted metadata. A sorted bounded vector
makes best-fit gap selection and release linear in the configured buffer limit;
there is no per-transaction allocation. Initial zeroing adds a write across the
padded extent. Measure against real IPC later rather than treating allocator
stress as a Binder benchmark.

Keep native memory ownership separate: the future Apple/Linux provider must
expose read-only guest bytes and a writable driver alias to the same backing
storage. Toggling a live guest view writable during a copy would create a race.
The portable allocator neither creates these mappings nor registers them with
the guest VM. Mapping references must outlive descriptor close until the guest
unmaps; transaction cancellation and arena teardown need tests at integration.
These are ordinary non-executable data mappings and need no new entitlement.

## 0093: Use the CI runner's real Binder module as the Linux reference

Status: official package/module/notice verified; the original 30-case ioctl
fixture passes on the real Linux kernel at c773122. Paired driver CI is pending.

The Ubuntu runner's `6.17.0-1022-azure` configuration enables Binder, but neither
a device nor installed module exists. Booting another kernel is unnecessary.
Its existing package index identifies the exact Ubuntu modules-extra package.
Pin that version and all required hashes, download it into the configured cache,
and extract only the unmodified module and GPL-2.0 copyright. No package manager
installation or kernel source import is needed. The module stays outside all
Apple builds and uploaded artifacts; original userspace test code remains MIT.

An explicit native-reference command requires the matching host release and
architecture, checks module vermagic/license, then uses the runner's existing
sudo/module tools. Refuse to replace an already-loaded module. Mount a private
binderfs in an empty owned build directory, create one fresh device and run the
ioctl fixture there. Require private mount cleanup and retain cleanup failures
as test failures. Windows/macOS preparation never loads anything. A changed runner
kernel requires a reviewed new package pin, not disabled checks or an assumed
success. This reference uses a native Linux host; ARTBox still has no guest kernel.

Keep reference execution and ARTBox comparison as separate evidence fields.
The first shared fixture defines ioctl copy/error ordering, prefix consumption,
thread exit and context-manager lifetime before driver implementation. Linux's
deferred close cleanup is polled with a bounded deadline; an instantaneous-close
assumption would encode the wrong contract. Subsequent transactions, references
and death notifications need additional paired cases.

The `d67b7f8` native attempt reaches the real ioctl fixture and shows that
BINDER_VERSION returns EINVAL for an invalid output address. The corresponding
Linux put_user error path deliberately selects EINVAL. MAX_THREADS and
CONTEXT_MGR_EXT also select EINVAL on copy failure; WRITE_READ selects EFAULT.
Correct the shared test before implementing the driver.
The earlier license check was also corrected from `GPL` to the module's exact
`GPL v2` declaration, verified in downloaded modinfo output.

Normal module removal returns EBUSY. Independent decoding of the pinned module
(453,745 bytes, SHA-256 `2b95b19919744a597d2d0074207729261ed4d83bbd83311979e82cc599a469b5`)
finds init_module and no cleanup_module. Options are to modify/build a module
with an exit path or retain the unmodified reference on a disposable host.
Choose the latter to preserve reference fidelity. Require an explicit
`--disposable-host` acknowledgement, unmount binderfs, wait for zero module
references and verify the non-forced unload syscall returns EBUSY. Record that
the module remains until the hosted test VM is discarded. No force removal or
claim of successful unload is permitted; ordinary developer workstations must
not be used for this native mode.

## 0094: Bound Binder endpoints before connecting VFS and delivery

Status: Linux and the original endpoint API pass the shared 32-case fixture at
517ad1c; local VM, resource and concurrency controls also pass. Broader CI is pending.

Use one private context object with independent per-open endpoints. Identify
endpoints with monotonically increasing tokens; reusing a metadata slot must
not let a stale callback act on a new endpoint. Reserve all endpoint and thread
metadata at creation, within explicit host limits. Keep guest MAX_THREADS
separate: setting that field to zero or UINT32_MAX must not change allocation
bounds or reject an already permitted caller. No per-ioctl allocation is needed.

Keep fixed virtual opener credentials for the initial single-process runtime.
Context-manager UID ownership survives close; busy ownership precedes UID
checking, and the extended object's guest copy precedes both. Mutable guest
credentials and SELinux are not implemented. The trusted owner must keep each
VM alive until close and in-flight calls complete. A context with live endpoints
cannot be destroyed. VM access is validated under the mapper lock, including
header copy-back after earlier command effects. Device state uses one mutex;
lock order is device then VM. Avoid exposing this API from VM callbacks.

This checkpoint exposes only version, threadpool limit, manager registration,
thread exit and looper writes. Receiving and recognized unimplemented work
return EOPNOTSUPP. The 64 KiB write work bound returns E2BIG explicitly; it is
an ARTBox admission limit, not a kernel ABI claim. Immediate synchronous close
satisfies callers that already tolerate the reference's deferred cleanup.
Future queues/mappings must retain their own storage lifetime after close and
must add cancellation/death tests before that path is connected.

Move shared fixture buffers into caller-owned storage so both a native process
and a real guest VM can execute the identical assertions. Required Linux CI
runs the kernel first, then builds and runs ARTBox's boundary, preserving the
two executable hashes and source provenance. Additional UID/resource/VM and
4,000 concurrent-lifecycle checks remain separately identified as ARTBox tests.
No syscall forwarding, runtime code generation, entitlement or CPU emulation
is involved. Context serialization and linear scans trade initial simplicity
for throughput; measure and revise them when transaction delivery exists.

## 0095: Pin Binder open descriptions outside the VFS dispatch lock

Status: VFS and real Linux pass the same 32 ioctl/22 descriptor cases at ba255d2;
local lifecycle tests pass. Full regressions are still running at this checkpoint.

Attach the private Binder context explicitly to a VFS before guest execution.
Keep it borrowed: its owner must retain it until the table and all callers are
gone. Each Binder open gets a shared open-description owner with a unique
endpoint token and originating VM. The owner closes that endpoint exactly once.
Allocate the description before opening the endpoint, then publish the guest FD
only on success. Failed opens must release all intermediate ownership.

For ioctl, copy the description reference while holding the descriptor mutex,
then unlock before entering Binder. An in-flight call therefore retains its
original endpoint even when its numeric FD closes and is reused. Holding the
table mutex during future blocking Binder reads would prevent other threads
from opening/closing or dispatching replies. Keep the established ordering
VFS then device then VM when nested; Binder must never call back into VFS while
holding its state mutex. Transaction FD transfer will need its own handoff plan.

Expose character-device metadata with virtual identities. Binder read/write
are unsupported methods, not the null/zero-device transfer paths; lseek is
non-seekable. Verify those distinctions using an original shared 22-case native
file fixture, separate from the 32 ioctl/endpoint cases. The native adapter
translates ARM64 O_DIRECTORY to the host's named constant rather than assuming
x86 and ARM flag layouts match. Cross-VM descriptor use, mmap/poll and attachment
to signed ART startup remain explicitly pending. Shared description allocation
costs one host allocation per open; no executable memory or entitlement is added.

## 0096: Observe receive-mapping lifetime without callbacks into Binder

Status: native Linux passes all 28 mapping/lifetime cases at 4d3a124. Passive
VM lifetime tracking and its local tests pass; Binder receive integration is pending.

The native fixture confirms that mappings retain context-manager ownership
after FD close, including after a partial unmap. Final unmap permits deferred
release. Remapping the same open remains EBUSY even after its VMA is gone.
Write-enabled mmap fails EPERM; subsequent write escalation fails EACCES.
The reference compares those results directly without reading unpopulated
receive pages or creating executable mappings.

A guest mapping cannot simply own a Binder open-description object whose
destructor enters the device mutex: file release happens under the VM lock,
opposing the device-to-VM lock order used by ioctl. Instead give the VM mapping
an optional passive watch with shared atomic LIVE/INTACT state. The watch retains
neither VM nor file, takes no VM/device lock, and invokes no device callback.
It survives VM destruction so the future Binder owner can reap closed endpoints
outside the VM lock. A state read is only a snapshot, not a pin against unmap.

Partial unmap or anonymous MAP_FIXED replacement permanently clears INTACT.
Protection changes preserve it. Failed native replacement invalidates it too,
because the VM is poisoned and its old page state is no longer trustworthy.
Dropping the last original file page releases the backing reference even when
anonymous pages remain in the reservation; LIVE then clears. This refines the
older reservation-wide file-reference lifetime from ADR 0024. The original
unwatched mapping API remains available without allocating watch state.

Tests cover early watch destruction, post-VM observation, partial unmap,
replacement, failed-map output preservation, protection changes, poisoned
replacement and 128 concurrent read/teardown cycles. Receive integration must
still retain the writable alias safely, keep context ownership while LIVE,
reject incoming allocations after loss of INTACT and preserve mapped-once
state after unmap. Descriptor-close flush/wakeup needs its own contract before
blocking Binder reads are enabled. Each watch adds bounded host metadata and
atomic state changes; it adds no executable memory or platform entitlement.

## 0097: Own Binder receive views independently of guest descriptors

Status: portable lifetime controls pass. Native provider and paired Linux
mapping comparison pass all 35 cases at db73182; 93 input hashes verify.

Configure a private backing factory before opening a Binder context. Each
successful receive mmap acquires two shared views of fresh, unlinked storage:
a writable view in a driver-owned VM and a read-only view in the guest VM.
Even guest MAP_PRIVATE uses shared receive bytes, as Binder requires. The
guest's write ceiling persists through mprotect. The factory uses the supplied
private root, descriptor-relative exclusive creation, unlink and ftruncate;
there is no global temporary path or executable mapping.

FD close invalidates the endpoint's call token but retains mapping resources
and context-manager ownership while the passive watch is LIVE. Subsequent
device operations reap closed endpoints after final unmap or replacement.
No VM release callback enters Binder. The closed endpoint drops its borrowed
guest VM pointer immediately, so VM destruction can precede deferred reaping.
An open that has mapped once cannot map again after unmap. VFS mmap holds an
open-description reference and releases its table lock before entering Binder,
allowing descriptor close during an in-flight map.

The backing limit is explicit per endpoint, up to 4 MiB; requests beyond it
fail ENOMEM rather than silently extending host storage. Both aliases reserve
virtual space, while resident pages depend on use. Unused bytes are zero-backed,
unlike Linux Binder's demand-populated pages that fault on invalid reads. This
is a documented initial difference, not a passing comparison of those faults.
Fixed receive maps and nonzero offsets remain unsupported. Transaction delivery
must still reject new allocations after loss of INTACT, impose async quotas,
and arrange close wakeups before enabling blocking reads.

The existing 28-case native mapping fixture now also drives guest VFS mmap,
expanded to 35 cases with read-only and write-only descriptor checks.
Additional controls cover allocation/alias-map failure cleanup, cross-VM use,
anonymous replacement, VM-before-device teardown and concurrent close/map.
The native provider test additionally checks driver-to-guest byte coherence
and absence of retained directory entries. Windows runs injected ownership
controls; Mac/Linux must run native backing and the real Linux reference job
must compare the same fixture before mapping compatibility is reported.

## 0098: Preserve virtual process identity for context-manager transactions

Status: the first native ping fixture fails at 517e126. At 200b910 its corrected
positive path and same-PID rejection control pass; 95 input hashes verify.

Two independent Binder opens do not make handle-zero self-calls legal. Linux
compares the sender's PID with the context manager's owner and returns
BR_FAILED_REPLY when they match. The initial fixture incorrectly expected a
second pthread in the same process to bypass that rule. The retained native
failure and read-only kernel-source review expose this before driver delivery
code is added.

Keep same-PID rejection as a negative control. For the positive native Linux
reference, prepare the manager first, then fork a client that opens its own
endpoint. A server pthread handles delivery; both owners are joined before
cleanup. A client using an inherited Binder open would retain the original
owner identity and is not a substitute. Fork exists only in the native Linux
test adapter, not in the portable runtime or iOS.

The portable fixture adapter uses distinct trusted virtual PIDs for the two
service/app owners while running them on host threads. That preserves the
driver's identity rule within a single host process. It does not implement
guest fork, process isolation or general multi-process applications. The same
fixture checks sender identity, reply bytes and completion/free command flow;
reference-object transfer and death notifications remain subsequent contracts.

## 0099: Start transaction delivery with bounded synchronous byte parcels

Status: native reference passes at 200b910. At bf36338 the production driver
passes the same fixture; artifact provenance and all 96 input hashes verify.

Connect the existing receive arena to BC_TRANSACTION, BC_REPLY and
BC_FREE_BUFFER. Copy source bytes through the VM into a bounded snapshot, then
into the receiver's independent writable alias. BR_TRANSACTION/BR_REPLY expose
only that receiver's guest address and immutable published buffer. Deliver to
a registered looper or the precise requesting TID. Preserve virtual sender
credentials and the context manager's pointer/cookie. Match the native PID
rejection rule before allocating transaction storage.

Reserve 1,024 active-call slots and 1,024 pending-packet slots per context.
Per-thread completion counters and error slots require no allocation during
peer teardown. Each receive arena permits at most 1,024 live buffers. Temporary
payload snapshots are bounded by the configured receive size. This first path
serializes under the context mutex; it is not a throughput result. No packet
storage is executable and no additional platform entitlement is introduced.

Mapped descriptor close retains the endpoint until final unmap. Final owner
loss cancels its calls, reports dead replies to live callers, removes pending
packets and clears abandoned buffer extents before releasing the driver view.
A queued-call close/unmap control runs with injected backing on every host;
this is a local teardown invariant, not yet a paired Linux race comparison.

O_NONBLOCK is carried from the VFS into the endpoint; empty reads return EAGAIN.
Blocking empty reads, nonzero initial read-consumed values, invalid buffer frees,
oneway/nested calls, objects/FDs and handles other than zero remain unsupported.
There is one outstanding synchronous call per thread in this first subset.
Those limits keep incomplete work explicit while the shared ping/reply test
establishes actual byte delivery. Reference translation, death notifications
and real servicemanager acceptance remain required to complete M4.

## 0100: Keep Binder references and death acknowledgments separate from owner lifetime

Status: at 1f9f107, all three shared Linux/ARTBox lifetime cases pass. The
downloaded artifact and 96 source hashes verify independently.

Retain context-manager strong/weak references using immutable endpoint tokens.
Closing and unmapping the owner releases its endpoint even while remote
references remain; those references still identify the dead owner. A newly
registered manager cannot take over an old subscription merely by reusing an
endpoint slot. Reference increments on the owner itself fail EINVAL; unmatched
reference/death commands and duplicate subscriptions are consumed without
creating work, following the observed kernel command boundary.

Reserve separate pools of 1,024 reference records and 1,024 death records.
Clearing detaches a subscription from its reference immediately, permitting a
new subscription while the old clear completion is pending. A death record
moves through armed, queued, delivered and acknowledged states. Clearing a
queued or delivered death waits for BC_DEAD_BINDER_DONE before returning
BR_CLEAR_DEATH_NOTIFICATION_DONE. A cleared live or already acknowledged
subscription can complete immediately. An opaque 64-bit cookie is copied as
data; it is never dereferenced. Owner loss queues work without allocating.

The shared fixture runs an actual synchronous call whose recipient closes
without replying, requiring both completion and BR_DEAD_REPLY. Its three cases
cover live cancellation, clear before death acknowledgement, and subscription
after owner loss with acknowledgment before clear. Wrong cookies, duplicate
requests and unmatched acknowledgments are negative controls. The native Linux
child explicitly closes its inherited manager descriptor before opening its
own endpoint; otherwise it could accidentally keep the parent's owner alive.
This fork remains native-reference-only. ARTBox runs trusted virtual owners on
host threads.

Injected local controls additionally check mapped-FD retention, last-reference
release, overlapping clear/new-subscription lifetime, and manager-slot reuse.
These are local invariants until individually paired with Linux. General
object/handle translation, node reference callbacks, blocking/poll wakeups and
real servicemanager remain required. Reacquiring a replaced manager while an
old handle-zero reference survives is explicitly unsupported until general
handle allocation exists. Pool exhaustion returns ENOMEM as an ARTBox admission
limit, not a claim about Linux allocation-failure notification behavior.

## 0101: Verify exported-object lifetime before admitting general Binder handles

Status: at 1423d52, the original shared fixture passes on native Linux. Artifact
provenance and 96 input hashes verify; production comparison is false there.

A byte-only call to handle zero cannot register a service. The next reference
exports two occurrences of the same strong Binder object to a manager endpoint.
It checks that both become the same nonzero receiver handle, with no leaked
owner pointer or cookie. The manager retains explicit strong/weak references,
returns the handle in its reply, and frees the original receive buffer. The
owner must receive its original pointer/cookie, including both offset-table
entries and unchanged surrounding parcel bytes.

The manager then calls the retained service handle and receives a real reply.
It subscribes to that object's death and makes another call which the object
owner abandons by closing its endpoint. Completion, dead reply, object death
cookie and clear/acknowledgment flow must all arrive. The exporting owner must
receive and acknowledge its INCREFS/ACQUIRE callbacks before departure. Native
Linux uses separate process owners; the portable adapter will use trusted
virtual owners on host threads, as in the existing fixture.

Keep `object_driver_compared` false until the production driver executes this
same flow. Existing ioctl, mapping, synchronous byte and manager-death comparisons
remain required. This fixture is a prerequisite for actual AOSP servicemanager
integration, not a substitute for it. Weak-object transfer, file descriptors,
scatter/gather fixups, nested calls and oneway delivery need additional cases.

## 0102: Tie translated Binder references to receive-buffer lifetime

Status: at 74a6c9c, Linux and ARTBox pass the shared object flow and all three
malformed-parcel cases. Artifact provenance and 96 input hashes verify.

Assign each exported node a monotonic context-local identity, its owning endpoint,
and its opaque guest pointer/cookie. Each receiving endpoint gets its own handle
for that node. Repeated objects reuse that handle; passing a handle back to its
owner restores the original pointer/cookie. Routing a nonzero handle requires
a strong reference and delivers the node's own target fields. These values
remain guest data, never host callbacks.

Copy parcel bytes and offsets into bounded snapshots before translation. Check
the complete offset table and admit only strong BINDER/HANDLE objects. Track a
temporary reference for every translated object and for the target of a nonzero
handle call. Freeing or cancelling that receive buffer releases those holds.
Owner teardown releases all its buffers, references and node ownership without
allocating. Dead nodes remain identifiable while remote references survive.

Explicit strong/weak counts are separate from buffer-held counts. Valid libbinder
flows retain an explicit reference before freeing their parcel. Unbalanced guest
reference commands cannot steal the buffer's temporary hold in ARTBox; misuse
semantics beyond the shared fixture are not yet compared with Linux. Registering
an already-exported node as context manager remains unsupported.

Node INCREFS/ACQUIRE returns hold pending acknowledgments, so a remote release
cannot retire the local object before its owner processes the callback. Initial
callbacks target the exporting thread, including while it awaits a reply;
subsequent release work can go to a looper. Acknowledgment validates the original
pointer/cookie. Once no reference, buffer hold or callback remains, the node slot
can be reused with a new identity.

Reserve 1,024 node slots, 1,024 reference slots and 4,096 buffer-hold records per
context. Limit each parcel to 64 objects. Bounded linear scans avoid allocation
in teardown; their throughput cost remains unmeasured. On partial translation
failure, unwind all earlier holds and arena storage. Malformed snapshots or
unowned handles queue BR_FAILED_REPLY and stop write consumption at that packet;
header/command copy errors retain ioctl EFAULT. The shared negative cases use a
conflicting second cookie, truncated second object and unowned second handle,
then run the valid lifecycle to expose leaked state. Admission limits remain
explicit ARTBox errors; weak objects, FDs and scatter/gather are still unsupported.

## 0103: Serialize one-way Binder delivery by node and receive-buffer lifetime

Status: at f8b21fb, the complete shared one-way lifecycle passes on Linux and
ARTBox. Artifact provenance and 96 source hashes verify; all 19 host jobs and
iOS pass.

AOSP servicemanager sends one-way callbacks while processing synchronous calls.
Permit TF_ONE_WAY without occupying or changing the sender's synchronous call
state. Accepted one-way work belongs to the receiving node; closing its sender
must not retract queued callbacks. Return a transaction completion to the sender,
preserve its UID, and report sender PID zero, matching the kernel's lack of a
retained synchronous caller for this operation.

Each node records one active receive-buffer address. Subsequent calls to that
node stay queued until BC_FREE_BUFFER releases the active buffer. A different
node on the same endpoint remains eligible. Dispatch alone does not advance the
queue, and a failed guest copy does not publish or consume a packet. Receiver
teardown discards accepted callbacks without inventing synchronous dead replies.
The existing bounded work pool and buffer-held node references carry ownership;
no executable memory or new platform API is required.

The original shared fixture exports two different callback objects, sends four
one-way calls from inside a synchronous registration handler, and checks per-node
ordering plus independent-node delivery. It holds buffers across empty reads,
makes a synchronous call while handling a one-way callback, then waits for actual
sender death before releasing the buffer that admits the final queued callback.
Local injected controls additionally cover copy faults, sender close before any
delivery, receiver teardown, and arena cleanup. These controls do not claim native
alias coherence; the shared fixture must execute with real backing on Mac/Linux.

Linux's asynchronous half-arena quota and spam detection are not implemented;
ARTBox's documented byte/metadata admission bounds apply. TF_UPDATE_TXN, frozen
processes, threadpool wakeups, blocking/poll integration, FD transfer and nested
synchronous calls still need separate contracts. This is a prerequisite for real
servicemanager, not its acceptance test.

## 0104: Generate real servicemanager interfaces with a pinned AOSP host compiler

Status: native compiler execution passes on Mac ARM64 and Linux x86_64 at
49e3786. All 20 generated files verify byte-identical across hosts; both malformed
inputs are rejected. Real servicemanager execution remains required.

Use the five AIDL definitions listed by the Android 15 libbinder build, unchanged,
to generate their C++ interface, proxy and native-stub classes. Avoid handwritten
service-manager parcels or replacing the AOSP implementation with our own
registration table. These generated interfaces will be inputs to the real
libbinder/servicemanager build, alongside its pinned source.

Select AOSP's build-tools prebuilt at the same named Android 15 tag instead of
bootstrapping Soong, Bison, Flex and the entire compiler dependency tree. Its
manifest identifies the actual compiler source revision separately from the
interface revision. Preserve both identities, exact file hashes and the selected
dependency notices. Native profiles cover macOS arm64/x86_64 and Linux x86_64;
other hosts can prepare these inputs as data without executing a foreign binary.
No installer, translated CPU execution or tool binary inside the app is required.

Run generation twice in different output directories and require the same 20
nonempty output files and hashes. Reject a syntax error and an unresolved type
with compiler diagnostics. Generated artifacts include original AIDL and notices,
the tool's build manifest and license records, and independent provenance for
both inputs and outputs. Compiler/library binaries stay in the configured cache.
Hash/cache/path controls run on every CI host; the compiler itself must execute
on Mac and Linux. Generation is a dependency check, not Binder transport or
servicemanager runtime acceptance.

The first native attempt rejected mixed absolute import roots and relative input
filenames; run from the source root with `-I .`. Once generation succeeded, the
exact-output check exposed four additional Bn/Bp compatibility headers emitted
for the two parcelables. The compiler's pinned GenerateCpp implementation emits
all three header kinds for every definition. Include those headers in the exact
20-file inventory rather than permitting arbitrary extra output.

The next check showed that generated comments include the compiler's full
command line, including absolute temporary paths. Use its upstream
`--omit_invocation` option and retain the relevant options in provenance instead.
This keeps the output portable and permits strict byte-for-byte comparison
without rewriting generated source or filtering differences after generation.

## 0105: Compile the real Android libbinder profile before adapting its imports

Use the unchanged Android 15 libbinder kernel-IPC source selection, its five
generated service units and the eight libutils Binder support units. Build
Android ARM64 relocatable archives with the pinned NDK, preserving x18 and the
existing baseline ARMv8 profile. Do not substitute libbinder's RPC-only SDK or
select vendor/recovery defines to remove platform dependencies. Pin transitive
headers, including graphics constants and APEX declarations, without replacing
them with empty compatibility headers.

The build emits unresolved required and optional imports separately. Archive
creation is not a link or an execution test; actual platform dependencies must
be implemented, linked and tested before M4 acceptance. This inventory makes
that remaining work explicit, including real servicemanager's access-policy,
VINTF and event-loop requirements. No APEX loading stub is introduced here.

Reserve x18 in every unit. Disable exceptions/RTTI as in AOSP and use its
RefBase option to omit callstack diagnostics, retaining reference ownership.
Use the upstream Soong warning policy for C99 designators, missing initializer
fields and unused-but-set variables; the latter remains a visible warning.
Pin Soong's original configuration as supporting evidence. No source body is
edited to satisfy a newer compiler.

Validate generated-input provenance and exact bytes before compiling. Check
each object's target header and the archives' member inventories, then preserve
input/object/archive hashes, compiler flags, symbol inventory and corresponding
source with notices. Mac CI must build the same 49 units; Windows can use verified
CI-generated bindings without executing a foreign host compiler. This adds no
executable-memory permission and makes no device or service-execution claim.

## 0106: Wait outside Binder ownership locks and reuse delivered signal epochs

Establish the empty-read contract on native Linux before enabling production
blocking reads. A new kernel thread first returns NOOP because its initial
return flag is set; every ioctl exit clears that flag. The next empty read can
block. The reference verifies the actual ioctl before sending a non-restarting
signal, and requires EINTR with consumed write commands preserved.

Use one condition variable under the existing bounded device mutex. Publish
write-side effects before releasing that mutex to wait, then reacquire thread
state by TID: another thread's exit may move vector elements. VFS already pins
the open description outside its descriptor lock. Retain that pin until the
whole ioctl returns, including after descriptor close/reuse. Reject concurrent
entry under one trusted virtual TID and direct close of an active endpoint.

Reuse the delivered-interrupt epoch from the signal subsystem. Neither signal
handlers nor passive VM watch teardown may enter Binder's mutex. A 5 ms timed
wait checks those observations in ordinary context; transaction and other ioctl
mutations notify immediately. This accepts periodic idle wakeups and scheduler
latency until a tested notification bridge exists. It requires no code generation,
private entitlement or guest signal trampoline beyond the existing signal path.

Local controls cover initial returns, interruption with and without write-side
progress, a shifting thread vector, descriptor reuse, and owner death caused by
final unmap while a client waits. Native Linux uses a real signal; the portable
control injects the delivered epoch, so it does not prove native-handler delivery
through signed Bionic. Run the shared ping/reply fixture in blocking mode on both
sides as a separate progress check. Readiness polling, epoll, threadpool growth
and the real servicemanager loop remain required for M4.

## 0107: Separate non-consuming Binder readiness from wait registration

Use the native poll contract to introduce a readiness snapshot at the device and
VFS boundaries before adding persistent epoll state. Poll may allocate a Binder
thread, but repeated observation must preserve its initial-return flag and queued
work. Reuse ioctl's bounded admission rather than creating a separate polling
thread table. Report POLLERR when that admission is exhausted.

VFS retains the open description while releasing its descriptor mutex before
the device call. Reject stale descriptors, foreign VMs and unsupported types
explicitly. The snapshot does not hold a subscription, access guest buffers,
consume transactions or change entered-looper state. Process-level readiness
can be visible before ENTER_LOOPER; retain separate predicates for readiness
and the current entered-looper read-routing contract.

Test the shared initial-return/error lifecycle against Linux, then expand it to
death readiness before registration and later consumption. Rerun the existing
IPC lifecycle fixtures through readiness checks without removing their original
nonblocking-read mode. Host admission and owner controls complement the native
comparison. This establishes the observation operation required by an event loop;
it does not claim a guest poll/epoll syscall, persistent wait registration or
real servicemanager execution. Those need separate lifetime/wakeup contracts.

The snapshot uses existing reserved, non-executable state. It adds no code
conversion, generated code, platform-specific call or entitlement. Its bounded
scans remain a performance cost to measure once the service loop runs.

## 0108: Keep native wake hints behind a small platform interface

Provide one owned wait/wake object per active event-loop waiter. A hint can be
posted before waiting and may coalesce with other hints; it is not a queue of
guest events or a replacement for the portable readiness predicate. The caller
must register before its final readiness check, then recheck after every wake.
One waiter per object prevents one thread from draining another's notification.
Concurrent signalers are supported; close requires all callers to have stopped.

Use Darwin's public `kqueue` user event with `EV_CLEAR` and `NOTE_TRIGGER`, a
nonblocking Linux `eventfd` with `poll`, and a Windows auto-reset event. Native
descriptors stay private. The implementation uses the ordinary OS SDK APIs;
no implementation from those projects is copied. See Apple's
[public event definitions](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/event.h)
and the Linux [eventfd contract](https://man7.org/linux/man-pages/man2/eventfd.2.html).
The shared core sees only create/signal/wait/close and negative Linux errors.
Do not swallow EINTR or retry a possibly completed close. No handler or passive
VM callback may invoke this interface; their existing ordinary-context epoch
checks remain necessary until separately tested notification paths exist.

Tests first reject an unimplemented provider, then exercise wake-before-wait,
coalescing, independent objects, 256 cross-thread handshakes, 16,000 concurrent
signals, timeout and repeated destruction/recreation. Mac/Linux additionally
interrupt the actual native wait with a non-restarting thread-directed signal.
The test runs in required host CI and the backend builds for iOS 15 from day one.
No JIT, executable mapping or entitlement is involved. Each active waiter costs
one native wait object and bounded host memory; event-loop resource limits and
guest epoll semantics remain separate work.

## 0109: Observe Binder file identity without retaining it through epoll

The native reference at `0cb0def` proves that a Binder receive mapping retains
an epoll interest after descriptor close. A new open reusing that descriptor
number has a distinct interest, and final unmap removes the original. A weak
reference to the VFS descriptor alone would discard the interest too early;
a strong epoll-owned open would keep the endpoint alive too long.

Use Binder's monotonic token to observe its existing file lifetime. Readiness
may observe a closed token while its passive receive watch remains live. It
must not dereference the old VM or reopen the file. Final unmap lets the existing
ordinary-context reaper remove the token; later observations return EBADF.
VFS still validates descriptors, and ioctl/nonblocking configuration still
reject closed tokens. The forthcoming epoll table must retain both the original
descriptor number and token so reuse cannot alias an existing registration.

Add at most 64 ordinary-context listeners per Binder device. This is a host
resource bound, not a Linux driver quota. Each listener has a monotonic
subscription ID, borrows its callback context and owns no endpoint. Mutations
and observed reaping publish hints under the device mutex; removing a listener
under the same mutex makes callback teardown synchronous. Listeners may signal
the native wake primitive, but cannot acquire VFS/VM locks, reenter Binder or
wait for guest work. A later VFS wait hub must preserve that lock order.

One shared change hint avoids callbacks retaining endpoint or thread addresses
that can move or expire. The cost is bounded broadcast and spurious rechecks;
measure it with the actual service loop before introducing targeted queues.
Signal handlers and VM teardown remain passive and need ordinary-context
polling. Tests cover mapped-token lifetime, callback fanout, exhaustion, stale
subscription reuse, unsubscribe during ongoing ioctls, and notifications after
ordinary-context final-unmap detection. This adds no executable memory or
entitlement and does not itself implement guest epoll.

## 0110: Implement guest epoll around file identity and native wake hints

Add the ARM64 epoll family to a six-argument VFS dispatcher. Preserve the
four-argument file API for existing callers, but route signed native syscalls
through the full entry so epoll_pwait cannot lose its mask pointer or size.
Encode the 16-byte ARM64 event explicitly, with data at offset eight; host
x86_64 epoll uses a different layout. Return errors for unsupported mask
substitution, nesting and edge/oneshot/exclusive modes. The pinned AOSP Looper
uses the implemented level-triggered ADD/MOD/DEL operations.

An epoll descriptor owns its bounded interest vector. Each wait pins that
epoll open across descriptor close/reuse, while interests hold only fd/token
identity for Binder. Polling reaps expired tokens and rotates through ready
interests when the output capacity is smaller than the ready set. Copy faults
do not consume Binder work; a partial copy returns the number already delivered.
The process VM and configured providers outlive the descriptor table and calls.

Use one device subscription per configured VFS and one native wake object per
active sleeping call. Register the waiter before the final readiness scan so a
change racing with sleep leaves a retained hint. A separate hub mutex protects
waiter publication and removal; its callbacks never enter VFS, Binder or VM.
The lock order is VFS, then device, then hub. Drop VFS/device locks before native
wait. Synchronous subscription removal precedes hub destruction.

Finite waits keep one monotonic deadline across spurious hints. Ready events
take precedence over an observed interruption. Passive mapping watches and
signal epochs still need ordinary-context checks every 5 ms; accept that idle
wakeup and scheduler cost until a separately tested notification path replaces
it. The signed runtime starts with 1,024 interests per epoll and 128 concurrent
waits; embedding hosts can configure lower or higher documented bounds.

Tests precede dispatch implementation and reuse the native 31-case readiness
fixture. Additional controls cover resource and foreign-VM rejection, mapped
fd reuse, fair scans, partial output faults, concurrent waiters, epoll close and
reuse during death wakeup, interruption and deadlines under repeated hints.
The native wake implementation remains the only platform-specific piece. No
executable allocation, JIT or entitlement is introduced. Eventfd/timerfd and
real servicemanager integration are still required; this is not full epoll
compatibility or M4 acceptance.

## 0111: Establish eventfd counter and wait behavior before Looper integration

Pinned AOSP Looper creates a nonblocking eventfd, registers it with epoll and
uses eight-byte writes/reads to wake its loop. Implement that ABI as a portable
counter, separate from the platform's private wake object. First run an original
shared fixture against native Linux on x86_64 and ARM64. Cover unsigned argument
truncation, ordinary and semaphore reads, saturation, readiness, transfer sizes,
copy faults and descriptor errors. The fixture uses operation callbacks so the
same expectations can later exercise guest syscalls without exposing host FDs.

Kernel source review identifies two easily missed rules to verify experimentally:
eventfd writes require exactly eight bytes, and a read consumes its counter before
copying to an inaccessible destination. Do not make that fault transactional.
Native controls also cross a protected-page boundary for both reads and writes.
No Linux kernel implementation is copied; see THIRD_PARTY.md for the reference.

Observe the exact read, write or epoll_pwait syscall in the worker's native
`/proc/self/task/TID/syscall` before releasing or interrupting it. Verify all three
waits with a real non-restarting signal and through descriptor close/reuse. A
separate persistent-registration test retains a target through a duplicate,
reuses its descriptor number, and confirms that final close removes the original
interest. This is a test-only host oracle, not a guest proc dependency, guest dup
implementation or proof that eventfd/Looper executes in the signed runtime.

## 0112: Keep eventfd state portable and share bounded wait admission with epoll

After the native reference passes on Linux x86_64 and ARM64 at `ca55328`, add
eventfd2 to the existing VFS entry. The descriptor table owns the counter,
initial flags and VM identity. Its mutex serializes counter changes, so the
implementation needs no host eventfd, additional kernel-facing API or second
counter lock. Encode the eight-byte Android value explicitly. Creation requires
the configured wake provider already used by the signed runtime's epoll path.

Pin an open description for the entire read/write, releasing the descriptor
mutex before a native wait and before read copyout. A pending operation keeps
the original counter through descriptor close/reuse. Epoll interests hold weak
counter references, not the current occupant of an fd number. They can observe
an old counter while an in-flight transfer retains it, and are reaped after
that final owner leaves. Binder interests retain their existing token/mapping
identity. No generic duplication or cross-VM descriptor sharing is implied.

Blocking counter I/O shares the epoll hub's bounded wait slots. Publish each
private native wake owner before rechecking the counter; every counter mutation
sends coalescible hints. Recheck ready state before interruption, preserving the
same delivered-epoch policy as Binder and epoll. The 5 ms ordinary-context check
remains a documented idle cost. Semaphore reads decrement by one; normal reads
consume the whole count before copyout. Never roll back on EFAULT. User writes
cannot reach UINT64_MAX, and zero writes remain valid at saturation.

The shared 63-case fixture precedes the implementation. Further tests cover
three real-provider wakeups, close/reuse followed by injected interruption,
weak-interest removal, semaphore contention, shared wait admission, native
provider failures, copy faults across a protected page, foreign VM rejection,
descriptor exhaustion and 2,048 concurrent increments. Native Linux interruption
and ARTBox's injected delivery remain different evidence. Anonymous-inode stat,
fcntl/dup, general I/O readiness and signed Looper execution are still pending.

## 0113: Test monotonic timerfd ordering and expiration counts before translation

Real servicemanager creates a CLOCK_MONOTONIC timer, arms a relative five-second
period and registers it with Looper. Add a shared original userspace fixture
before implementing that virtual descriptor. Native x86_64/ARM64 tests cover
the 32-byte itimerspec layout, unsigned syscall argument truncation, malformed
times, new-input validation before fd/flag checks, and timer replacement before
an old-value output fault. Disarming discards unread expirations while retaining
the requested interval; periodic gettime advances deadlines without consuming
the expiration count. Establish these behaviors in CI rather than assuming
timerfd is an eventfd with an extra timeout.

Use a past absolute monotonic deadline for ready-state checks and a bounded
readiness loop instead of assuming a sleep is sufficient. Periodic reads must
include missed intervals. Separately observe native read and epoll_pwait workers
inside the exact syscall before rearming, interrupting or closing/reusing their
descriptor. There is no simulated kernel or production host-proc dependency.

Timerfd's read copyout differs from eventfd: a partially copied result may return
a positive byte count before faulting. Record the observed prefix length on each
architecture and require expiration consumption even on a complete copy fault.
The amount a kernel copy primitive manages before a page fault is not a portable
byte-for-byte guarantee. Do not reuse eventfd's all-or-error copy behavior without
testing this boundary. The reference is original MIT code; Linux source is
consulted only for behavior under its separate license. Guest timerfd remains
unimplemented at this checkpoint.

## 0114: Account monotonic timer expirations without one host timer per descriptor

After `fb53373` passes the 59 shared assertions and six native wait cases on
Linux x86_64 and ARM64, add portable monotonic timerfd state to VFS. Store its
next deadline, interval and unread ticks under the descriptor mutex. Refresh
from the host monotonic clock at read, gettime and readiness scans. Periodic
refresh advances the deadline by whole missed intervals while retaining unread
ticks. Zero initial value disarms; replacing a timer discards the previous
unread count even if copying the old value subsequently fails. Saturate valid
timespec conversion and relative-deadline addition instead of overflowing.

Use the existing bounded wait hub and its ordinary-context checks at 5 ms
intervals. This costs idle wakeups and can add up to a check interval plus host
scheduling delay to timer observation. It allocates neither a native timer nor
a helper thread per guest descriptor. Keep this explicit performance limitation
until service execution and measurements justify a separately tested wake
deadline optimization. Realtime, boottime and alarm clocks return EOPNOTSUPP;
there is no invented suspend or wall-clock cancellation behavior.

Active reads retain their original timer through close/reuse; epoll interests
hold weak identity. Read consumes ticks before copying page by page, returning
the successful byte count if a later page faults. The native protected-page
reference returned four bytes on x86_64 and one on ARM64. ARTBox returns the
page-bounded prefix (four in that fixture), a documented difference in fault
granularity, not a claim of identical kernel copy instructions. Normal eight-byte
transfers and expiration consumption are shared contracts.

Tests run before implementation and add real-provider read/epoll wakeups,
injected interruption after close/reuse, partial-copy consumption, provider
failures, fd/VM admission and weak-interest removal. A controlled monotonic
clock verifies nanosecond boundaries, missed periods, clock errors and an old
timer waking its retained read after the descriptor number is reused. This
avoids scheduler-dependent guesses about when a test worker has run. Shared
VFS test adapters keep eventfd/timerfd byte encoding and wake observation
consistent. Real AOSP Looper/servicemanager execution remains required for M4.

## 0115: Establish real AOSP Looper behavior before signed service integration

Use the unchanged Android 15 Looper and Timers units, with the already pinned
RefBase/Vector support, rather than implementing a replacement event loop.
They require the eventfd/epoll/timerfd boundary now tested against Linux. The
full libcutils/APEX dependency graph is unnecessary for this Looper fixture;
servicemanager's remaining dependencies still require their own integration.

Run the same original MIT caller against native Linux on ARM64 and x86_64
before using it in the signed Bionic harness. Cover actual TLS ownership,
coalesced and cross-thread wake, auto-removal and modification of descriptor
callbacks, stable message ordering, delayed messages, cancellation and timer
callbacks. Require deliberate omitted-wake and retained-callback controls to
fail at their designated assertions. Observe Looper's own isPolling publication
for the thread handshake; do not call this syscall-observed kernel blocking.
Use bounded polls rather than treating a sleep as evidence of readiness.

Keep Linux libc/C++/AOSP host logging as the reference platform, and preserve
Android ABI branches in the separate NDK object build. Reserve x18/x27/x28 in
the latter and reject syscall, thread-pointer, reserved-register and unknown
instructions before signed packaging. Android logging/C++ imports must resolve
from the existing real signed dependencies later; do not add stand-in functions.
The native Linux run does not prove Android ABI or signed Apple execution.

Timers.cpp's clock selector comparison triggers sign-compare under the pinned
NDK. Soong's reviewed global.go disables this diagnostic globally. Retain the
warning and make it nonfatal only for that unchanged unit; record the exact
per-unit flag. Keep all warnings fatal for the original caller. This avoids a
source fork solely for a build-flag difference and preserves the diagnostic.

No ART image profile or iOS acceptance is changed by this reference checkpoint.
Actual guest Looper execution, Binder attachment and real servicemanager remain
required. No physical device is needed for this step.

## 0116: Execute Looper through a separate five-image signed Bionic profile

The unchanged AOSP Looper passes the shared 43-assertion caller and both
negative controls on native Linux ARM64 and x86_64 at ce43021. Next run that
same caller through Bionic and the portable eventfd/epoll/timerfd translation.
An import audit of the verified four-image ART bootstrap closure finds that
its C++/logging functions already cover Looper's needs. The missing public
eventfd function is supplied by selecting Bionic's original eventfd.cpp at the
existing Android 15 pin; its __eventfd syscall entry and fd tracking are already
present. Retain the full BSD notice and hash the selected source.

Add a fifth, image-local Looper library to that bootstrap profile. Keep the
15-image JavaVM/libcore acceptance separate. Reusing libart for existing C++ and
logging avoids duplicating or replacing these runtimes, at the cost of loading
ART code even for this native-only diagnostic. It does not start a JavaVM. The
existing pre-start JNI, shared-heap and sigchain checks still run. This is an
integration test profile, not a production servicemanager process design.

Link with all strong imports resolved and precisely the four named bootstrap
dependencies. Keep Looper support symbols local to its image. Reject ELF TLS
until explicitly integrated, audit the complete linked instruction stream, and
use the established build-time wrapper for ordinary signed macOS and iOS 15
frameworks. No runtime-generated executable memory or additional entitlements
are introduced. The shared result is a fixed-width C ABI in a platform header,
which also keeps existing harness corresponding-source archives complete.

Run normal, retained-callback and omitted-wake cases in separate native Mac
processes. A negative control must return its precise shared failure code,
rather than crash or time out. Require the single Bionic worker to be reaped
and signal/TLS/loader ownership cleaned up for every case. Preserve exact source
and binary identities, ordinary signatures, results, timing and stderr. A
successful Linux run, local cross-link or signed framework alone does not
establish guest execution; the actual Mac invocation remains the acceptance.

Binder is not yet attached to this profile. Real servicemanager policy,
node-count/threadpool support, registration, ping and death acceptance remain
M4 requirements. The diagnostic framework is not yet integrated into the app's
M4 IPA, and no physical iPhone execution is claimed.

Validation at `480238f`: all 22 host jobs and iOS CI pass. The downloaded signed
Mac artifact independently verifies 43 shared assertions, both precise failure
controls, and cleanup of one Bionic worker in each process. It contains verified
macOS and iOS 15 Looper frameworks with ordinary signatures. The normal fixture
takes 14.976 ms, with 28.089 ms loading/relocation and 16.873 ms bootstrap; these
are diagnostic Mac observations, not throughput or device measurements.

## 0117: Establish the node-count contract before servicemanager client tracking

Real ProcessState queries GET_NODE_INFO_FOR_REF for servicemanager's client
callbacks. Returning a fixed count or treating the handle's ACQUIRE total as
the node count would hide live clients. Add an original shared lifecycle
fixture to the existing native Binder oracle before enabling this ioctl in
the portable driver. Linux source remains a behavior reference only.

Use explicit synchronous call/reply barriers around reference acknowledgements
and buffer release. Hold both initial owner acknowledgements, then release the
weak and strong holds separately. Check one manager import, duplicate ACQUIRE/RELEASE pairs,
two object occurrences returned into their original owner's receive buffer,
and a separate importing endpoint. Keep the first endpoint alive while that
second import is acquired and released. Finally observe owner death before
querying the retained dead-node reference and dropping it to weak-only/missing.
These phases distinguish endpoint references from temporary local buffer holds
without relying on sleeps to make the count settle.

Manager authority belongs to its registered open, including when another open
has the same native PID and UID. Test all reserved/output input fields before
permission validation, invalid input pointers and output into the read-only
Binder receive mapping. Preserve exact negative errno and unchanged input on
the tested failures. The new native-result field is separate from
node_info_driver_compared, which stays false until the same fixture executes
through ARTBox. GET_NODE_DEBUG_INFO and other unneeded ioctls remain separately
scoped. No guest kernel, runtime executable generation or iOS entitlement is
introduced by this contract.

## 0118: Derive Binder node counts from existing ownership under the context lock

At fb0232b the actual Linux driver passes the node-info fixture, including
pending acknowledgements, retained local objects, a second importer and owner
death. Artifact provenance and all 105 input hashes verify. Enable the portable
ioctl with that same fixture in the native-backed guest transaction test.

Count one contribution for each importing endpoint with a strong or temporary
strong reference, independent of its ACQUIRE total. Add pending owner strong
acknowledgement and each local buffer claim; pending weak acknowledgement is
reported separately. Existing manager holds count while alive. A dead node has
only remote contributions. Deriving this snapshot under the existing context
mutex avoids adding counters to every transfer, rollback, acknowledgement and
teardown path. The cost is scanning up to 1,024 reference and 4,096 claim slots
per query, with no allocation. These admission bounds also prevent count overflow.

Copy the 24-byte input before validating its reserved/output fields, then check
the registered manager token and its strong handle. Use existing VM copies for
the output so its read-only fault cannot mutate Binder ownership. Keep generic
descriptor, TID admission and VM-owner checks ahead of the ioctl body. The local
test failed before implementation and now covers validation order, unreadable,
wrapped and truncated input, full ioctl-word matching and admission precedence.
Windows cannot run the native receive-backed lifecycle fixture; Mac/Linux CI must
establish that comparison. No physical-device execution is inferred.

## 0119: Audit Binder objects and retain their real compiler TLS

Before linking libbinder into the signed runtime, reserve x27/x28 as well as
Apple's x18 across all selected and generated units. Keep separate function/data
sections for later link-time collection. Audit every executable section against
the disassembly and reject syscall, thread-pointer, reserved-register and unknown
instructions. An empty disassembly is acceptable only with zero executable bytes;
AOSP's file.cpp is intentionally empty when libbase supplies its functions.

This check exposes BufferedTextOutput.cpp's actual TPIDR_EL0 read. Retain its
nontrivial thread_local state and initialization guard, instead of making them
global or disabling buffered logging. Reuse the existing global-dynamic TLS
adapter, pinned to the original source hash, both compiler TLS symbols, four
descriptor calls and one thread-pointer read. Reassemble the original compiler
output and require byte identity before assembling the adapted copy. The change
replaces the host thread-pointer base with zero for ARTBox's absolute TLSDESC
resolver; it does not alter the source, allocate executable memory or patch code
at runtime. The remaining instruction count must match the executable bytes.

The eventual link must disable TLS relaxation and register the additional TLS
module. Real C++ destructor registration remains an unresolved dependency until
its implementation and thread-exit behavior are verified. Costs include reserved
register pressure and descriptor-based guest TLS lookup; neither has a Binder
runtime measurement yet. The archive build alone cannot establish service
registration, polling, logging isolation or teardown correctness.

## 0120: Close Binder imports with original AOSP platform support

A focused ProcessState/Parcel link using the reserved-register Binder archives
and verified signed-runtime dependencies exposes the next required implementations.
Select nine original Android 15 system/core units: Threads, SystemClock, misc,
native_handle, multiuser, properties, ashmem-dev, trace-dev and libvndksupport's
linker. Pin only these files and their required build/header/notice inputs.
Keep the Android branches, real property callbacks, tracing error path and
dynamic-loader imports. Do not use recovery/vendor defines or replacements that
report success for unavailable kernel or loader features.

libcutils does not inherit libbinder's unique_fd conversion prohibition. Undefine
that macro only for ashmem-dev.cpp, which uses its normal implicit fd access while
retaining RAII ownership. Keep C++20 for tracing and its original atomic initializers;
make only the NDK's deprecated-pragma diagnostic nonfatal for trace-dev.cpp, with
the warning retained in artifacts. This follows Soong's existing nonfatal
deprecated-diagnostic policy without suppressing instruction or runtime checks.

The private focused link still requires timespec_get, fnmatch, android_dlopen_ext
and android_get_exported_namespace. The full servicemanager also needs VINTF and
an explicit access policy where Android uses SELinux. Compiling these archives
does not establish any of those behaviors. Subsequent tests must cover real
thread lifecycle and TLS destructors before attaching Binder to signed images.
The cost is additional signed code and dependencies; final linked size and
execution overhead remain unmeasured. No runtime code generation is introduced.

## 0121: Test Binder's libc imports through original Bionic on both kernels

The real Binder/platform link requires timespec_get and fnmatch. Before adding
their source selection, an original shared NDK caller fails to link against the
existing libc image with exactly those missing symbols. Select Bionic's original
time.cpp and OpenBSD-derived fnmatch.c; do not rewrite the wrappers or pattern
matcher. Keep their BSD/ISC notices and exact hashes.

Run the same caller object in the signed Bionic startup harness and a static
native Linux ARM64 executable. The oracle links the two original source units
explicitly before the NDK runtime, keeping Android's time-base and pattern
semantics rather than relying on another libc's extensions. Build it on the
existing NDK producer and execute it only on a native Linux ARM64 runner. Retain
its executable/object/source hashes, link map, source archive and NDK notices;
never copy this Linux executable into an Apple bundle.

The 50 expectations cover 36 C-locale pattern cases and 14 realtime/monotonic,
errno, output-boundary and invalid-base checks. Two controls clear FNM_PATHNAME
and substitute the realtime base for a monotonic request; each must fail at its
specific assertion in all three execution profiles. Require the new results
before staging Bionic or linking ART. Preserve the original fixed M2 denominator;
this is an additional mandatory M4 dependency contract.

The upstream time unit also defines timespec_getres. Its clock_getres syscall
remains unsupported and is not counted as implemented by this selection. CPU
clock bases and non-C-locale pattern semantics likewise remain outside this
contract. No executable-memory transition or runtime code generation is added;
the signed wrappers use the existing clock translation and precompiled code.

## 0122: Bind Android loader extensions to an explicit signed namespace

Original libvndksupport requires android_dlopen_ext and exported-namespace lookup.
Bionic's pinned linker uses a configured visibility map; a missing namespace
returns null without changing dlerror. It does not imply that every Android
namespace exists. Model one optional, explicitly named visible namespace per
immutable signed startup group. Existing unnamed contexts remain invisible.
Copy the name at creation and issue a context-specific opaque token distinct
from library handles. Do not infer vendor/sphal namespaces or host search paths.

Support null extension information, zero extension flags and USE_NAMESPACE for
that context's token. Open acquires the same reference and symbol scope as an
ordinary open in the group. Reject foreign/stale/null tokens and every other
extension bit before acquiring a reference. FD loading, new mappings, forced
copies, address reservations and RELRO sharing require separate implementations;
silently accepting them would violate their semantics and could bypass signed
code provenance. Dynamic namespace creation and namespace-to-namespace links
also remain unsupported. Accept the narrower runtime capability until a real
caller needs those features, without patching that caller to hide the request.

Decode the 48-byte Android LP64 extension record through the guest mapper rather
than dereferencing host-layout structures. Bound namespace-name reads like other
loader strings. Preserve thread-local pending errors on successful opens and
valid namespace queries. Invalid guest memory fails safely at the bridge.
Keep __loader_android_dlopen_ext restricted to libdl.so and namespace lookup to
libdl_android.so, matching the separate original AOSP frontends. Portable tests
precede implementation. The dedicated signed loader fixture now includes both
unchanged frontends in separate images, with 51 extension cases and two precise
negative controls, while retaining the 32-case ordinary Linux oracle comparison.
Require those signed results before linking ART. The ART runtime keeps its
15-image group and has no visible namespace configured; adding libdl_android and
attaching real Binder remains separate work. Do not claim the extension fixture
is an execution comparison against Android's full dynamic linker.

No writable executable memory or new runtime code is introduced. The costs are a
copied namespace name, integer-token validation and the existing guest-memory
snapshot/open lock; execution overhead is not yet measured. This is a declared
single-group namespace model, not Android's full linkerconfig isolation policy.

## 0123: Build original VINTF and generate its APEX parser on the host

The full Android servicemanager link reaches VINTF, kernel-version parsing and
APEX XML dependencies. Keep the real Android VINTF target implementation and
build its 27 original VINTF/kernel-config units, GKI's three libkver units and
TinyXML2. Do not remove Android branches or return placeholder manifests to
avoid those dependencies. SELinux header declarations are required to compile;
they do not supply the policy-version query or an access-control implementation.

Generate the APEX parser using the original 39-unit AOSP XSdc Java compiler and
the original schema, with the same tinyxml, writer, root and package options as
Soong. Pin its Apache Commons CLI 1.2 binary, corresponding source and notices.
Building the original compiler is preferable to a handwritten generated-header
facsimile or a new parser with subtly different field/optional-value behavior.
The existing portable JDK runs only on the host. Preserve raw output, normalize
CRLF to LF for shared golden hashes and reject any other output difference.
Generate twice from fresh directories and require malformed-schema/unknown-root
controls. Cross-platform CI must reproduce the reviewed four-file output.

Use a separate VINTF build entry point with an optional verified XSdc artifact.
It reuses the tested ARM64 object audit and retains all unresolved symbols. Use
quote-only private-header search to avoid case-insensitive Regex.h collisions.
Follow the pinned Soong diagnostic policy narrowly and keep the warnings visible;
the original stack VLA and unused constants do not justify editing upstream code.
Artifacts carry original/generator sources, exact flags, notices and object hashes.

All generated C++ is compiled ahead of time for the future signed framework.
No device-side compiler, executable-memory transition or new entitlement is
introduced. The costs are a host JDK/generator step and additional signed code;
final linked size and runtime overhead remain unmeasured. Successful archive
compilation establishes neither service execution nor full XML schema validation.

## 0124: Exercise original Bionic regex before attaching real VINTF

The Android VINTF path calls Bionic's POSIX regex API. Add the four unchanged
NetBSD-derived implementations from the already pinned Android 15 Bionic tree,
using its original libc_netbsd compile flags and compatibility headers. Keep
the production libc and Linux reference source hashes pinned together. The
reference uses the NDK's ordinary Linux syscall/TLS runtime; it is never an
Apple bundle input. No host regex replacement or source-level regex adaptation
is introduced.

Write the Android LP64 caller before selecting these units. Its first link
against the preceding signed-runtime ELF fails on all four public regex
functions, demonstrating the missing dependency. Compile that same caller once
with NDK headers and use the identical object in the native Linux ARM64 oracle
and both signed Bionic profiles. Preserve the existing 50 libc checks and M2's
fixed denominator; report 120 new regex expectations separately. Cover BRE/ERE,
backreferences, POSIX character classes, leftmost-longest matching, captures,
newline/anchor flags, bounded embedded-NUL input, compile errors, truncated
error strings, ABI canaries and repeated allocation/free. Deliberately remove
REG_NEWLINE and truncate the capture count; each must fail at its exact check.
The native Linux comparison verifies both caller hashes, the complete reference
source archive and executable identity before running it. Both signed profiles
must pass before staging their device frameworks or linking ART.

Regex compilation produces ordinary heap data, not executable instructions.
This adds signed native code and guest allocations without a runtime compiler,
executable-memory transition or entitlement. Full linked size and execution
cost are still to be measured. C-locale regex behavior is the current contract;
broader locale behavior, libc++ regex helpers and actual VINTF/service execution
require separate evidence. Successful object compilation is not that evidence.

## 0125: Preserve early iOS diagnostic failures in the app container

The first physical M0/M1 launch succeeds, but the integrated ART diagnostic
returns to SpringBoard before reporting a result. The acceptance harness can
print a failure and call `_Exit`, which neither produces a crash report nor
drains queued UIKit log updates. Redirect stdout/stderr to a cache file before
UIApplicationMain and mirror console messages there before UI dispatch.
Overwrite this file on each launch so repeated tests do not accumulate logs;
failure to open it must not prevent startup. Retrieve the file before relaunch.

This uses ordinary container file I/O and adds no debugger, entitlement, runtime
code generation or jailbreak dependency. The supplied jailbroken phone is
useful for hardware diagnosis, but its installation path cannot establish
ordinary stock-device provisioning. Record the two evidence scopes separately.
The logging cost is synchronous, unbuffered diagnostic I/O; it is not intended
as a performance benchmark configuration. The underlying ART exit is still an
unresolved failure, not an accepted slow path or a successful device test.

## 0126: Bound Scudo's primary reservation for ordinary iOS address spaces

The `930f17d` M2 IPA fails on an iPhone 6s Plus running iOS 15.8.5 when the
original Android Scudo configuration reserves 8,858,370,048 bytes (8.25 GiB).
The native mapping returns ENOMEM, Scudo reports an internal map failure and
Bionic exits. This occurs before the allocator test, despite green Mac CI.
The installed executable is byte-identical to the CI artifact. The new cache
log provides the syscall and allocator error without attaching a debugger.

Keep Scudo and its AndroidNormalConfig, changing only the ARM64 primary region
exponent from 28 to 24. Its 33 contiguous regions then reserve 528 MiB instead
of 8.25 GiB. Generate the configuration through Scudo's existing custom-header
hook from the hash-verified original; retain both headers and all notices.
The unadapted compilation control keeps exponent 28. Size classes, region
randomization, compact pointers, shared TSDs, release policy, secondary allocator
and GWP-ASan remain the original implementation.

Alternatives were separate lazy region mappings or replacing the primary with
Scudo's 32-bit allocator. Both change more allocator behavior. Enlarging the
device's permitted address space through entitlements is outside the project's
constraints. The smaller primary uses the existing larger-class/secondary
fallback when a region fills; this can increase mappings, fragmentation and
allocation cost under pressure. It adds no runtime code generation.

Make both signed M2 processes use a 1 GiB total VM reservation budget, preserving
all 328 existing expectations and reporting this additional contract separately.
A retained 24 MiB single-size allocation workload must survive primary exhaustion,
preserve every byte and support realloc. This turns the hardware failure into a
repeatable Mac constraint. ART retains its separate VM ceiling, but uses the same
bounded Scudo build. Full signed execution and device retesting are required
before calling the candidate a fix; its performance cost is not yet measured.

## 0127: Keep optional ARM64 CRC instructions behind Scudo's feature dispatch

The smaller primary maps successfully on the test iPhone at `2f23685`, then
Scudo faults with SIGILL at a `crc32cx` in Allocator::allocate. Mapping the
recorded PC through the pinned ELF and `__libc_globals` address identifies the
instruction. Android's global `-mcrc` enables Scudo's unconditional inline CRC
path, bypassing its AT_HWCAP check even though ARTBox advertises HWCAP=0.

Compile native Scudo for the existing ARMv8-A baseline, enabling `-mcrc` only
for its original `crc32_hw.cpp`. The untouched dispatcher then selects its
software checksum with the current conservative auxiliary vector. Retain the
Android upstream compilation control. Audit every native Scudo object: CRC
instructions are rejected outside that one helper, which must contain exactly
one. The audit rejects the preceding allocator object before changing flags.
The resulting build additionally outlines the original `scudo::computeChecksum`
as a weak definition; record that exact source-backed symbol in the existing
profile-difference policy. All other symbol and instruction checks remain required.

This retains Scudo's original checksum implementations and introduces neither
CPU emulation nor runtime code generation. Software checksums cost additional
instructions; throughput is unmeasured. A future optimized profile must report
host-verified capabilities before enabling the optional path. No CPU feature
is inferred from the fact that the app uses arm64 or targets iOS 15.

## 0128: Size the signed ART arena independently of its reference encoding

At `31c9a4a`, the iPhone 6s Plus passes the complete Bionic suite, but the ART
package finishes all 38 constructors and exits at `ART shared heap binding`.
That pre-start check requests a 4 GiB window. Unbinding it retains the native
reservation until VM destruction; the real runtime would then request another
4 GiB. All 16 installed Mach-O images match the verified CI package byte for
byte. The cache log establishes the failed binding, without an errno result
from that older harness. Mac acceptance alone missed this device limitation.

Use four native pages for the preliminary null-reference/binding contract and
a 512 MiB managed arena for the signed diagnostic's 128 MiB Java heap maximum.
The arena accommodates ART's separate spaces while avoiding the full
compressed-reference range. The encoding remains a 32-bit base-relative offset,
validated against the actual window; it does not require every possible offset
to be reserved. Keep the native Linux reference's full 4 GiB mapping, class
table, card table and large-object bitmap coverage unchanged.

Enforce a 1536 MiB reservation ceiling in the shared signed ART harness, including
Bionic, guest mappings and retained windows. This is a guest-owned VM budget,
not a process RSS or total host address-space limit. Report both window sizes,
the ceiling and actual reserved bytes. Bootstrap, signed-runtime and device
packaging checks reject missing or oversized evidence. Before adding that
validation, ten negative cases reproduce the missing acceptance safeguard.
Existing GC, exceptions, thread-state, lifecycle and console controls remain
mandatory. Failed binding now reports its requested bytes and Linux error.

Alternatives were releasing/reusing the preliminary window or making the
reference arena grow. Early release introduces reference-lifetime concerns;
growth complicates stable offsets and contiguous address ownership. A small
retained preliminary window keeps the current ownership contract. Applications
with larger heaps will need a separately validated configurable budget; a
512 MiB arena is the diagnostic policy, not an Android compatibility guarantee.
No executable mapping, entitlement or runtime code generator is introduced.

The first candidate (`bfa34db`) used a 1 GiB arena and 2 GiB ceiling. All 22 host
jobs and iOS passed; signed Mac execution reserved 1,631,879,168 bytes and
completed every managed check. On the phone, the four-page preliminary binding
and Bionic VM worker succeeded, but the 1 GiB arena returned ENOMEM before
JNI_CreateJavaVM. The 512 MiB follow-up keeps the same Java heap maximum and
tests; its lower total ceiling also rejects the preceding Mac reservation
footprint. Failed arena binding now records preexisting guest reservations
to make budget consumption visible alongside the mapping error.

At `2970c38`, all 22 host jobs and iOS pass, and the 512 MiB arena completes
real ART execution on the iPhone. Hello DEX, collection, exceptions, attachment,
shutdown, background/foreground and cold home-icon relaunch pass without a
debugger. Both phone runs reserve 1,095,008,256 guest bytes, within the 1536 MiB
ceiling. All 16 installed code images match CI. This validates the chosen
diagnostic budget on that device; larger heaps and ordinary provisioning remain
separate work. See [M3 device evidence](acceptance/m3.md#successful-physical-device-execution).

## ADR 0129: Run the original VINTF parser against one shared Android caller

The full servicemanager link needs three libc++ regex helpers. Select the
original NDK r28c `regex.cpp.o` archive member, pin its bytes and license, and
audit its executable sections like compiled Binder objects. Reimplementing
these helpers or disabling VINTF would replace Android behavior before it has
been measured. The existing source pin already includes KernelConfigParser;
this step adds no further AOSP checkout.

Compile its unchanged implementation and the MIT contract caller once. Link
those exact objects into an original-NDK Linux reference and a fifth signed
framework using the four ART bootstrap dependencies. Require native Linux
execution and matching object/source identities before signed Mac acceptance.
The shared caller returns 89 assertions and exact -104/-105 controls for omitted
comment and relaxed-format behavior. Pre-start heap/sigchain checks and owner
cleanup remain part of the Apple diagnostic; no JavaVM or Binder service is
started by this fixture.

Regex state is ordinary heap data. All executable bytes are wrapped and signed
at build time, with empty entitlements and iOS 15 deployment targets. Both
frameworks retain VINTF, header, NDK/LLVM and ARTBox notices. This dependency
contract leaves SELinux policy replacement, trusted endpoint identity and
isolated servicemanager/client globals for separate tested changes. At
`136a087`, all 89 assertions and both controls pass on native Linux ARM64 and
signed Mac. Independent verification confirms shared object identity, source,
notices and both signed framework layouts. Parser calls take 0.153 ms on Mac
in this run. The iOS wrapper is verified without claiming device execution.

## ADR 0130: Fixed guest credentials precede servicemanager access policy

The original IPCThreadState queries getuid when restoring its own caller
identity. Returning ENOSYS cannot represent a service process; using Darwin's
UID would expose the app container account instead of an Android identity.
Copy four explicit virtual credentials into each kernel-thread descriptor and
inherit them on clone. The existing initializer keeps app ID 10000; the new
initializer accepts distinct real/effective UID/GID values from the trusted
runtime owner. Reject UINT32_MAX, preserve unsigned return values, and leave
setuid/setgid and group mutation unsupported.

Tests compare real Linux query semantics and verify distinct identities,
configuration lifetime, rejected initialization and clone inheritance. This
does not bypass ServiceManager's app-UID registration rejection. The owner must
use the same identity when configuring Binder and signals. Bionic's cached PID
and libbinder's process globals still need separate manager/client bootstrap
state before this can establish real service isolation; four syscall results
alone do not provide that isolation or a sandbox for hostile native code.
At `5fead21`, all 23 host jobs and iOS pass, including the Linux comparisons
and existing signed Bionic/ART regressions. The integrated ART IPA verifies
independently with empty entitlements.

## ADR 0131: Explicit service grants replace no SELinux behavior implicitly

The Android Access implementation depends on SELinux, while its host branch
allows every request. Neither represents the intended ARTBox service boundary.
Use an original portable policy with explicit calling PID/UID, operation and
service-name grants. Empty configuration denies everything, including root;
there are no wildcards or implicit system privileges. List permission requires
a separate explicit grant. Keep Android's existing app-UID registration check
in the real ServiceManager implementation.

Copy at most 1024 rules and their names during construction, then permit only
immutable queries. Duplicate or malformed configuration fails construction.
Names use the original service contract's bounded ASCII alphabet. Separate
real/effective guest credentials stay owned by the runtime; the eventual Access
adapter must consume original IPCThreadState calling identity, populated by
Binder endpoint metadata, rather than identity bytes in the Parcel payload.
This is an application service policy, not SELinux compatibility or a native
code memory sandbox. Process-state separation remains independently necessary.

The portable contract covers explicit grants, unknown identities/services,
root denial, operation confusion, case/prefix mismatches, embedded NUL and
invalid names, configuration ownership, size limits and concurrent queries.
At `84bfde3`, all 23 host jobs and iOS pass; the integrated ART IPA verifies
independently. Connecting the policy to real servicemanager, signed guest
execution and the complete M4 service acceptance remain separate pending work.

## ADR 0132: Preserve original servicemanager and make policy adaptations explicit

Compile the pinned Android 15 main and ServiceManager without source edits.
Supply the original Access interface with an MIT adapter that queries real
IPCThreadState calling PID/UID and the immutable explicit-grant policy. A
trusted owner configures it once before starting service dispatch; missing
configuration denies requests. The policy remains owned for the guest image's
process lifetime. Original Access.cpp is reference-only, avoiding both a false
SELinux claim and the original host branch's allow-all behavior.

The separate `security_policyvers` boundary returns -1/ENOTSUP. Original VINTF
logs that fetch failure but its aggregate fetch returns OK while retaining the
previous/default version. The caller contract tests this exact distinction;
aggregate success must never be interpreted as a supported SELinux policy.
Access contracts also require actual Bionic/IPCThreadState identity, with
wrong-client-UID and wrong-manager-PID controls. Their execution is pending the
isolated guest harness; compiling them is not service acceptance.

Retain the original platform ID header under the include spelling expected by
ServiceManager, without changing its bytes. Audit every compiled instruction,
archive source/notices and record unresolved imports. Each role needs separate
signed images, Bionic cached PID/TLS, libbinder globals and VM/VFS/thread owners;
the shared Binder device alone connects them. No JavaVM is needed for M4.

## ADR 0133: Test three independent Bionic roles around the original service loop

Use distinct signed framework install names for manager PID/UID 100/1000,
provider 101/1001 and app client 102/10000. Each role receives its own load
group, Bionic TCB, libbinder globals, VM, descriptor table, futex and signal
owners. They share one portable Binder device with trusted endpoint identity.
The host verifies that dyld did not reuse code/data addresses across roles.
Six images per role reuse the established native C++ dependencies without
starting Java or reserving its managed arena. This costs three Scudo primaries
and duplicates signed images; measurements must determine the device impact.

The manager executes unchanged AOSP main indefinitely. Observe successful
context-manager registration before starting its peers. Retain its context,
images, worker and backing owner for the host process lifetime; never report
complete daemon cleanup. Provider/client calls are finite, run on independent
native threads and are joined before descriptor close and VM unmap. That
teardown must cause the original BpBinder death recipient, with no synthesized
callback. The fixture also checks 32 sequenced payloads, caller credentials,
denied registration/listing and original app-UID rejection.

Run the Access contract and wrong-UID/PID controls in separate processes. Its
identity checks construct IPCThreadState, which original main explicitly
forbids before setting call restrictions. The real daemon therefore receives
only the tested grant configuration before entering main. Both positive and
negative contracts require exact observations. This new signed execution gate
is pending; local compilation/linking does not establish M4 completion.
