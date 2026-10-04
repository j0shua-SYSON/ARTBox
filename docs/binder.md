# Userspace Binder

M4 is in progress. The current implementation validates the 64-bit Android
Binder wire boundary and implements an initial endpoint/ioctl API. It does not
deliver transactions, run servicemanager or complete M4. A configured VFS now
exposes `/dev/binder` for the tested ioctl subset; mapping and polling remain
pending. Recognizing other commands does not implement those operations.

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
the UID restriction survives its owner's close. Context-manager removal is
immediate in this synchronous boundary; callers must still tolerate Linux's
deferred release. No host credentials or descriptors are exposed.

VERSION, MAX_THREADS, CONTEXT_MGR/EXT, THREAD_EXIT and WRITE_READ's three looper
commands are enabled. Every copy uses the guest VM. A failed write preserves
completed prefix consumption and zeroes read consumption; header copy-back
failure takes precedence. Full ioctl/command words are matched. Thread-exit
reclaims the calling TID, which a later ioctl recreates. Unknown words fail with
EINVAL; recognized work not implemented yet, including receive operations and
transactions, fails with EOPNOTSUPP.

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
descriptors and directory/exclusive opens. Its real Linux comparison is pending
CI. Portable controls cover relative lookup through `/dev`, failed-open cleanup,
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
guest alias. A native read-only guest mapping, VM registration, async quotas and
driver-close lifetime are not integrated yet. The synthetic-address host test
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

## Evidence

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
hashes and all core/platform input hashes. The new VFS/file comparison is
pending CI; it adds a third executable hash and retains separate case counts.

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
service AIDL and license files; these are not built or shipped yet.

The selected AOSP code is Apache-2.0. Linux's
[Binder driver at v6.12](https://github.com/torvalds/linux/blob/v6.12/drivers/android/binder.c)
is a GPL-2.0 behavior reference only; no kernel implementation is copied or
distributed. ARTBox's original portable implementation and tests remain MIT.
