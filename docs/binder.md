# Userspace Binder

M4 is in progress. The current implementation validates the 64-bit Android
Binder wire boundary and implements an endpoint/ioctl API. Its first synchronous
byte-parcel delivery path awaits paired execution; servicemanager and M4
acceptance remain incomplete. A configured VFS now
exposes `/dev/binder` for the tested ioctl subset and configured receive mappings.
Polling remains pending. Recognizing other commands does not implement them.

## Boundary implemented

`core/src/binder_wire.c` decodes immutable host snapshots without allocating,
casting unaligned wire structs, or following guest pointers. It recognizes
all 19 write commands and 22 return encodings in the pinned Android 15 UAPI.
It matches the entire Linux command word, including direction and size.
Unknown or truncated commands leave the cursor/output unchanged; earlier
successful frames stay consumed. This parser cursor is separate from
the endpoint's `binder_write_read.write_consumed` handling below.

Transaction views preserve 64-bit addresses and lengths and distinguish the
scatter/gather extension from the return security-context extension. Object
tables require eight-byte entries, four-byte object alignment, known types,
complete objects and ascending non-overlapping extents. The seven object
encodings have sizes 24, 32 or 40 bytes. This structural check does not validate
handle ownership, strong/weak references, file descriptors, parent fixups,
sender credentials, transaction flags or buffer lifetime.

The public interface uses decoded values and borrowed byte views, not native
packed structs or the host's ioctl definitions. In particular,
`binder_handle_cookie` is 12 bytes on Android ARM64; Darwin ioctl numbers and
a compiler's ordinary struct padding cannot define this boundary.

## Endpoint/ioctl boundary

`binder_device` owns a private context and independent state per open, including
for opens from the same virtual PID. Monotonic tokens never reuse a closed
endpoint's identity. A fixed virtual UID owns the context-manager registration;
the UID restriction survives its owner's close. An unmapped manager is removed
on close; a mapped one remains until the final original page disappears and a
device operation reaps it. Callers must tolerate deferred release. No host
credentials or descriptors are exposed.

VERSION, MAX_THREADS, CONTEXT_MGR/EXT, THREAD_EXIT and WRITE_READ's three looper
commands are enabled. Every copy uses the guest VM. A failed write preserves
completed prefix consumption and zeroes read consumption; header copy-back
failure takes precedence. Full ioctl/command words are matched. Thread-exit
reclaims the calling TID, which a later ioctl recreates. Unknown words fail with
EINVAL; recognized work outside the implemented subset fails with EOPNOTSUPP.

Host limits reserve metadata for at most 1,024 endpoints and 1,024 threads per
endpoint. These are explicit admission bounds, independent of MAX_THREADS's
Linux threadpool setting. Endpoint exhaustion returns EMFILE, thread exhaustion
ENOMEM, and write batches over 64 KiB E2BIG. The first implementation serializes
state under one context mutex, with lock order device then VM. No transaction
performance claim is made; these limits and scans need measurement once IPC
exists. See ADR 0094 for ownership and integration requirements.

The same original fixture can run in real Linux userspace or in VM-owned
storage through ARTBox. The native 30-case baseline passed at `c773122`; the
current 32-case fixture additionally covers registration without a spawn
request. At `517ad1c`, both the real Linux kernel and ARTBox pass all 32 cases;
the downloaded artifact, PR merge parent and 87 project input hashes verify.
Local driver checks also cover UID persistence, stale tokens,
read-only/partial/wrapped guest buffers, exhaustion, unsupported operations,
and 4,000 endpoint lifecycles across eight threads. Those extra controls are
ARTBox invariants, not additional Linux comparisons.

## Guest descriptor integration

`artbox_vfs_set_binder` attaches a borrowed context before guest execution. Each
open of `/dev/binder` acquires a new endpoint; close and table destruction release
it. The context must outlive the table and all calls. Ioctl pins the open
description under the VFS mutex, releases that mutex, then calls the device.
This preserves endpoint identity through descriptor reuse and avoids holding
the entire table during future blocking Binder operations. Cross-VM descriptor
use is explicitly unsupported in the initial single-process model.

The same 32-case fixture also runs through actual virtual openat/ioctl/close.
A separate 22-case descriptor fixture checks character-device type, unsupported
read/write including zero length, non-seekability, access-mode errors, closed
descriptors and directory/exclusive opens. At `ba255d2`, both VFS and real Linux
pass all 32 ioctl and 22 file checks; the artifact, PR parent and 90 source
hashes verify. Portable controls cover relative lookup through `/dev`, failed-open cleanup,
table teardown, descriptor/command argument widths and 4,000 concurrent opens.
Virtual device/inode numbers are ARTBox identities, not copied host metadata.
The signed ART startup does not attach this context yet; guest-native Binder
acceptance will follow receive mapping and transaction support.

## Receive-buffer ownership primitive

The bounded portable arena reserves metadata once, admits aligned receive
buffers, accepts driver writes while reserved, and makes them immutable to its
API when published. A delivered buffer can be released; a reserved buffer can
be cancelled. Exact allocation starts and ownership states are required. It
zeros new/reused extents including padding and supports clearing on release.
Empty transactions consume eight bytes so concurrent buffers have distinct
addresses. Exhausting bytes, contiguous space or metadata returns an explicit
error without altering the output. Best-fit admission scans the bounded buffer
list; it does not allocate memory per transaction.

Tests cover ownership transitions, interior/duplicate release, overflow,
zero-length transactions, extent boundaries, fragmentation, recycling,
byte/metadata exhaustion and 8,000 lifecycles across eight concurrent workers.
This is an internal storage primitive, not a Binder ioctl implementation or
Linux quota comparison. The caller must provide valid writable storage and its
guest alias. The arena is not yet connected to transaction delivery or async
quotas. The synthetic-address host test
does not establish native alias permissions.

`test_binder_mapping` exercises the existing native file/VM providers with two
shared views of an unlinked backing file: writable in the driver's VM,
read-only in the guest's VM. It requires byte coherence, guest protection
ceilings, clear-on-free visibility, guest-view survival after driver-side close
and final unmap. A separate child attempts a raw write and must fault at that
exact address. The signal handler exits directly, avoiding crash/core artifacts.
Apple/Linux must execute both checks. Windows reports this check unexecuted
while its native file provider is unavailable; the portable arena tests still
run. This fixture verifies primitives for integration, not `/dev/binder` itself.

The VM now has optional passive mapping watches. INTACT clears on partial
unmap or anonymous replacement; LIVE clears after the last original file page
is gone, even if anonymous reservation pages remain. Watches retain no file or
VM and are readable after VM teardown. They avoid callbacks into Binder under
the VM lock; their snapshots do not pin memory. Local tests include poisoned
replacement and 128 concurrent observation/teardown cycles; see ADR 0096.

The endpoint/VFS receive path now owns separate writable driver and read-only
guest aliases through a configured private backing factory. Closing a descriptor
retains context-manager ownership until the last original file page disappears;
later calls reap it without VM-to-device callbacks. The same open cannot remap
after unmap. Portable checks pass the shared 35-case mapping fixture, failure
cleanup, replacement, cross-VM rejection and close during an in-flight mmap.
At `db73182`, native alias coherence and paired Linux mapping comparison pass;
the artifact, PR parent and 93 input hashes verify. Unused
bytes are zero-backed rather than Linux's faulting demand-populated pages;
fixed mappings, nonzero offsets and transactions remain unsupported. The fixture
adds seven access-mode checks to the earlier 28-case Linux baseline.
See ADR 0097.

The next shared transaction fixture uses separate endpoints and a real second
thread for handle-zero ping/reply. It checks caller credentials, target/cookie,
receive-buffer bounds, payload bytes, both completion messages and consumption
of both buffer-free commands. Reference waits are bounded and endpoints are
nonblocking. The first native attempt at `517e126` exposed Linux's rejection of
handle-zero calls from the manager's own PID, even with independent opens.
The revised fixture retains that rejection control and opens the positive
client in a separate native Linux process. ARTBox's adapter uses trusted virtual
PIDs across host threads; no runtime fork is introduced. See ADR 0098.
At `200b910`, both native controls pass and 95 source hashes verify.
The production driver now queues synchronous handle-zero byte parcels, replies,
completion messages and buffer frees through the receive arena. At `bf36338`,
the required native-backed VFS test passes the same fixture on Linux and Mac;
the Linux comparison artifact and all 96 source hashes independently verify.
Error/owner controls pass locally, including delayed failure after a mapped
manager's final unmap. Empty nonblocking reads return EAGAIN. Blocking empty
reads, nonzero initial read-consumed, invalid buffer frees, nested synchronous
calls and FD transfer remain unsupported. Strong-object and one-way support are
described below. See ADR 0099 for the original synchronous byte boundary.

Manager strong/weak references and death subscriptions now use bounded pools
independent of endpoint lifetime. Opaque cookies survive owner loss; clearing
detaches a subscription immediately but waits for its delivered death to be
acknowledged before returning clear completion. Local controls cover live
cancellation, duplicate/wrong cookies, last-reference release, retained mappings
and endpoint-slot reuse. Three shared native cases additionally abandon an
actual synchronous call and require its dead reply. All three Linux/ARTBox cases
pass at `1f9f107`; artifact provenance and 96 input hashes verify. See ADR 0100
for resource limits and remaining general-handle work.
Blocking reads, polling and actual AOSP servicemanager still need implementation
and acceptance.

The next shared fixture exports a repeated strong object, retains and calls its
receiver handle after freeing the original parcel, verifies return-to-owner
pointer/cookie restoration, and observes the exported owner's death. It passes
on native Linux at `1423d52`, where `object_driver_compared` remains false.
Production translation now uses bounded node/handle tables and buffer-held
references. Local controls verify driver-alias bytes, callback acknowledgments,
free-triggered release and partial-failure rollback. The shared native-backed
driver test adds the complete lifecycle and three malformed-parcel cases. At
`74a6c9c`, both Linux and ARTBox pass, with artifact provenance and 96 source hashes
independently verified. Weak objects, FDs and scatter/gather remain unsupported.
See ADRs 0101 and 0102 for exact scope and resource bounds.

One-way calls now use per-node queues. A delivered buffer retains the node's
active slot until FREE_BUFFER, allowing other nodes on that endpoint to proceed.
The sender gets completion without a reply, and sender teardown preserves accepted
receiver work. Local queue/fault/teardown controls pass. The new shared fixture
checks four callbacks across two nodes, sends from a synchronous handler, issues
a synchronous call while holding a one-way buffer, and requires final delivery
after sender death. At `f8b21fb`, Linux and ARTBox pass this same lifecycle; the
downloaded artifact and 96 source hashes verify. Linux asynchronous byte quotas,
spam detection and transaction replacement remain unsupported; explicit ARTBox
admission bounds apply. See ADR 0103.

## AOSP servicemanager build inputs

`python -B scripts/binder_aidl.py` generates the five actual AOSP interfaces on a
native macOS arm64/x86_64 or Linux x86_64 host. It fetches only that host's pinned
compiler and C++ dependency, source definitions and notices into configured
`ARTBOX_CACHE_DIR`. Output defaults to
`ARTBOX_BUILD_DIR/m4/binder-aidl/<profile>`; `--output` selects another directory.
An existing generated tree must match the newly verified output before reuse.

Other hosts can run, for example,
`python -B scripts/binder_aidl.py --prepare-only --profile darwin-arm64` to verify
all inputs without execution. Preparation is not a compiler test. Native CI must
generate 20 C++/header files twice with identical hashes and reject malformed
syntax and an unresolved type. Artifacts retain original source, notices and
provenance without the host binaries. This builds an input to servicemanager;
it does not yet compile or run the service. See ADR 0104 and `THIRD_PARTY.md`.

## Evidence

`tests/native_binder_wait.c` establishes the blocking-read contract before the
production wait implementation. The native worker is observed in its exact
Binder ioctl through `/proc/self/task/<tid>/syscall`, then interrupted by a
scoped SIGUSR1 handler without SA_RESTART. The two interruption cases require
EINTR, preservation of consumed write commands, zero read consumption and the
already-written leading NOOP. Separate invalid-buffer and O_NONBLOCK controls
require EFAULT and EAGAIN. Worker observation and joins are bounded; a stuck
worker fails the disposable reference process. Linux execution is pending for
this addition, and `wait_driver_compared` stays false. Host `/proc` inspection is
test-only and does not become part of the portable Binder implementation.

The first run at `c70452f` observed an immediate four-byte NOOP on a newly
allocated kernel Binder thread. Linux initializes `looper_need_return` for that
thread and clears it when the ioctl returns. The fixture now requires that
initial event explicitly, then observes and interrupts the following empty read.
This avoids treating initialization as a blocking-read failure or overlooking
the first-read behavior in the future production implementation.

The portable test covers all command encodings, every truncated write frame,
unaligned storage, wrong direction/type/size, preserved prefix progress,
full-width transaction values, packed death cookies, object boundary cuts,
duplicate/reversed/overlapping/unaligned/overflowing offsets and unknown types.

`fixtures/binder-wire/uapi.c` independently uses NDK r28c's real
`linux/android/binder.h`. Compile-time assertions compare 60 constants,
protocol version, structure sizes and field offsets. Its ARM64 object emits
a 156-byte stream and parcel using those UAPI structures. LLVM extracts the
data section; the native host parser consumes it. No ARM64 instructions execute
on an incompatible host. The required macOS host CI step runs:

```sh
python3 -B scripts/test_binder_wire.py --host-test build/host/test_binder_wire
```

The report records the NDK revision, UAPI/header/source/object/fixture hashes
and host reader hash. This is an ABI comparison, not a live Linux Binder
semantic oracle or an iPhone execution claim.

## Next contracts

1. Add bounded read-only receive mappings to the VFS boundary, preserving
   mappings after descriptor close until unmap.
2. Synchronous transactions/replies across threads, thread-affine reply stacks,
   copied payloads, explicit buffer release and resource exhaustion.
3. Node/handle translation, reference acknowledgements, ordered one-way queues,
   polling/threadpool wakeups, close/death/clear/done races and invalid commands.
4. Build the pinned real AOSP servicemanager and libbinder with reviewed
   dependencies and narrow platform adapters. Test registration, discovery,
   ping/pong and death notification through distinct driver endpoints.

Every contract needs failure controls before being enabled. Kernel comparison
needs an actual native Linux Binder device. Its private binderfs context now
runs on CI; missing access must not be reported as a passed comparison.
`scripts/probe_binder.py` records available devices/protocols, installed module
paths and Binder kernel configuration without loading anything. Its default
success means the inventory completed; `--require-device` fails unless an
accessible protocol-8 device exists. It does not run lifecycle tests.
If modules are missing, it records exact matching package metadata from the
runner's existing package index without updating, downloading or installing.
Multi-process APKs, FD transfer, buffer-parent fixups, scheduling/priority
inheritance and SELinux enforcement are outside this first boundary checkpoint.

## Native kernel reference

The Ubuntu runner uses `6.17.0-1022-azure` with Binder configured as a module,
but the module package is absent. The matching official package and its single
Binder module were downloaded and hash-verified; eight package/cache and
execution-mode controls pass locally. `scripts/binder_reference.py` prepares those pinned
bytes on any host. The explicit `--run-native --disposable-host` mode requires
the matching Linux kernel, existing compiler/module tools and passwordless sudo. A required
Linux CI job loads the unmodified module, mounts a private binderfs below the
build directory and creates one fresh context for an original ioctl fixture.
It then unmounts, requires zero module references and checks the normal unload
attempt returns EBUSY. The pinned module has no exit hook; it remains loaded
until the disposable hosted runner is discarded. Logs and byte provenance are retained.
No package installation, kernel boot or CPU emulation is involved.

The fixture tests version/canaries, invalid pointers and ioctl words, thread
limit inputs, empty write/read, a valid command prefix followed by an invalid
command, resumed consumption, thread exit/recreation, legacy/extended context
manager registration, duplicate ownership and delayed close/re-registration.
At `c773122`, the native 30-case fixture passed. At `517ad1c`, the expanded
32-case fixture passes both Linux and ARTBox. Artifact digests, project source
hashes and PR merge-parent identities verify. Native
execution and ARTBox comparison remain separate: `--compare-driver` requires
native execution, builds the portable endpoint test and compares its shared
fixture result only after the reference succeeds. Record both executable
hashes and all core/platform input hashes. The VFS/file comparison passes at
`ba255d2`, adds a third executable hash and retains separate case counts.

At `4d3a124`, the original mapping fixture passes 28 native checks for rejected write
permissions, zero/unaligned inputs, private/shared mappings, write-protection
ceilings, partial unmap, repeated mapping and context-manager ownership across
descriptor close/final unmap. It never reads unused receive pages: Linux has
not populated those pages with transaction data. The artifact digest, PR parent
and 92 input hashes verify. These are native Linux results;
`mapping_driver_compared` remains false until the production mapping boundary
runs the same fixture. The existing alias test is not that implementation.

The first live attempt at `d67b7f8` confirmed two contract corrections before
any ARTBox ioctl implementation: BINDER_VERSION's invalid output pointer returns
EINVAL rather than EFAULT, and the module cannot be unloaded. Its decoded ELF
has `init_module` and no `cleanup_module` symbol. Private mount cleanup remains
required; a positive reference count is a failure, and force unloading is never
used. Native execution is restricted to explicitly disposable test hosts.

The `aa82e23` attempt also observed EINVAL for an invalid MAX_THREADS input.
Reviewing every fixture ioctl's copy path confirms that CONTEXT_MGR_EXT uses
EINVAL too, including before the already-owned context check. WRITE_READ's
invalid argument uses EFAULT. These expectations are request-specific. That
attempt successfully unmounted the private context and verified normal unload
returns EBUSY with zero remaining module references.

## References and source scope

`binder-references` in `third_party/sources.json` pins 15 exact reference files
from AOSP `frameworks/native`, tag `android-15.0.0_r1`, commit
`f7274fca5e36082674740bc6c976f73c4578d009`. The selection includes the real
`cmds/servicemanager` implementation/build/test, Binder process/thread paths,
service AIDL and license files. The separate [native build](binder-build.md)
now compiles the real libbinder kernel-IPC profile and libutils support using
generated AOSP interfaces. The real servicemanager and signed runtime attachment
remain pending; the Binder libraries are not embedded in the IPA yet.

The selected AOSP code is Apache-2.0. Linux's
[Binder driver at v6.12](https://github.com/torvalds/linux/blob/v6.12/drivers/android/binder.c)
is a GPL-2.0 behavior reference only; no kernel implementation is copied or
distributed. ARTBox's original portable implementation and tests remain MIT.
