# Userspace Binder

M4 is in progress. The current implementation validates the 64-bit Android
Binder wire boundary; it does not expose `/dev/binder`, deliver transactions,
run servicemanager or complete M4. Recognized commands are not advertised as
implemented driver operations.

## Boundary implemented

`core/src/binder_wire.c` decodes immutable host snapshots without allocating,
casting unaligned wire structs, or following guest pointers. It recognizes
all 19 write commands and 22 return encodings in the pinned Android 15 UAPI.
It matches the entire Linux command word, including direction and size.
Unknown or truncated commands leave the cursor/output unchanged; earlier
successful frames stay consumed. This cursor is a parser cursor, not yet
the kernel's `binder_write_read.write_consumed` implementation.

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

1. Per-open endpoint state, protocol/version/context-manager ioctls, bounded
   read-only receive arenas and command-buffer validation through the VM layer.
2. Synchronous transactions/replies across threads, thread-affine reply stacks,
   copied payloads, explicit buffer release and resource exhaustion.
3. Node/handle translation, reference acknowledgements, ordered one-way queues,
   polling/threadpool wakeups, close/death/clear/done races and invalid commands.
4. Build the pinned real AOSP servicemanager and libbinder with reviewed
   dependencies and narrow platform adapters. Test registration, discovery,
   ping/pong and death notification through distinct driver endpoints.

Every contract needs failure controls before being enabled. Kernel comparison
needs an actual native Linux Binder device; its availability on CI has not been
established. Missing access must not be reported as a passed kernel comparison.
`scripts/probe_binder.py` records available devices/protocols, installed module
paths and Binder kernel configuration without loading anything. Its default
success means the inventory completed; `--require-device` fails unless an
accessible protocol-8 device exists. It does not run lifecycle tests.
Multi-process APKs, FD transfer, buffer-parent fixups, scheduling/priority
inheritance and SELinux enforcement are outside this first boundary checkpoint.

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
