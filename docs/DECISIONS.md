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
