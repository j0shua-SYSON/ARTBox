# Controlled Bionic startup test

This test is the integration step after the syscall and string slices. It links
the complete current Bionic source selection into `libc.so`, then links a separate
NDK allocator client with a real `DT_NEEDED` dependency on that library. Both
images use signed Apple wrappers. Runtime writes are limited to their data pages;
guest instructions retain their ELF addresses and are never generated or patched.

The original bootstrap adapter uses the pinned Bionic definitions for shared
linker globals, the temporary TCB and static TLS layout. It calls AOSP's early,
late and final main-thread initialization. Initialized GWP-ASan state occupies the
native-bridge TLS slot. The host retains its own thread pointer. The bootstrap
adapter has no stack protector because Bionic reseeds its guest stack guard while
that frame is active; the allocator client retains its stack protector.

The test runs on a native pthread with a VM-owned, guarded non-executable stack.
Its initial argument block contains a Linux auxiliary vector with the actual page
size and host-generated secure AT_RANDOM bytes. Constructors run in ELF priority
order after relocation and TLS setup. Writable image storage is padded to native
pages so Bionic's WriteProtected globals can enforce their own read-only state.

The NDK client checks real pthread identity and errno, then malloc, usable size,
realloc preservation, calloc zeroing, alignment, overflow and strdup/free over
sizes from one byte to one MiB, plus device I/O: 146 checks. This fixture does not yet create guest
threads, implement a filesystem, load arbitrary modules, support guest ELF TLS
templates, run guest exit destructors or satisfy the complete M2 acceptance suite.

The fixed test load group is libc plus the client. An absent optional
`libnetd_client.so` follows AOSP's documented fallback; any other dynamic library
request fails the test. Unimplemented clone, fork, namespace and teardown imports
have test-failure endpoints. They never report success. Unimplemented syscalls
return ENOSYS and are recorded. The host never resolves Android symbols from its
own libc. These explicit fixture bindings are not a production loader namespace.

Run `python -B scripts/test_bionic_startup.py` after building the host and both
Bionic profiles. Windows validates the ELF inputs and instruction boundary;
macOS builds/signs both iOS 15 and macOS frameworks and executes the signed Mac
pair. The first signed execution at `203b8b6` completed real TLS setup and entered
libc's priority-1 constructor. It then aborted after exhausting AT_RANDOM:
GWP-ASan requested another byte while `/dev/urandom` was absent. The virtual-device
implementation makes the normal AOSP entropy path available without modifying
Bionic's fallback or pretending that an absent file exists. Native completion
passes with that fix at `5671845`. Diagnostics and source,
object, framework and notice hashes are retained in the `bionic-startup` artifact.

## Verified execution

Implementation `5671845` passes [host and Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34759298382)
and the [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34759298361).
All 13 portable contracts pass, including native Linux comparisons for supported
device operations. The signed Mac pair completes 146 checks, three constructors,
one absent optional netd lookup and 127 syscalls. Unknown calls remain recorded
as ENOSYS; signals, futex wake, scheduler queries, property-file stat, prctl and
logging sockets are not made successful by the fixture.

One measured run takes 3.387 ms to read/load/relocate the images, then 3.707 ms
for native pthread setup, Bionic initialization and the client, including tracing.
This is a correctness run, not isolated allocator throughput. The VM tracks
8,864,874,496 reserved bytes, mostly Scudo's 8.25 GiB primary address reservation.
That is virtual address space, not committed or resident memory. Reducing this
reservation and measuring resident memory remain necessary iOS work.

Downloaded ELF, signed macOS/iOS container bytes, load biases and every notice
match their reports. libc ELF SHA-256:
`c6a5135b0a781713ee6167e17bf212e303755a358a961acafd22c0456660b29a`;
client ELF SHA-256:
`21644a7083513fecacd2e4e58153103c842844f700b185fee1e781fa73a5122b`.
Physical iPhone execution is unverified. This is substantial M2 startup progress;
the mandatory guest threads, regular files and file-backed mappings remain open.

The current test also requests a second fresh process with
`GWP_ASAN_PROCESS_SAMPLING=1`, `GWP_ASAN_SAMPLE_RATE=1` and
`GWP_ASAN_MAX_ALLOCS=32` in its guest environment.
This uses AOSP's existing options. The same client must pass and at least one
observed allocation must belong to the initialized GWP-ASan pool. Both modes
pass at `118a0bb`; the sampled run observes 30 guarded allocations. The purpose
is to cover the otherwise randomly selected startup path. Setting the pool
capacity explicitly avoids AOSP increasing it inversely with the artificial
sampling rate; the 32-slot capacity passes CI at `ea5fd75`, with 30 observed
guarded allocations and 8,866,004,992 total reserved bytes. This does not test
recovery from memory faults or claim signal support.

The next test adds a 19-case NDK futex caller to both startup modes and compares
its identical object with Bionic's original/adapted syscall entries on Linux.
It uses the real Bionic errno path and the process's native wait domain.
