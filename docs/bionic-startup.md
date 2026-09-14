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
sizes from one byte to one MiB, plus device I/O: 146 checks. The thread extension
creates real Bionic workers and runs their key destructors and exit paths. This
fixture does not implement a regular filesystem, load arbitrary modules, support
guest ELF TLS templates, run process exit destructors or satisfy the full M2 suite.

The fixed test load group is libc plus the client. An absent optional
`libnetd_client.so` follows AOSP's documented fallback; any other dynamic library
request fails the test. The Bionic pthread clone and final thread-exit boundaries
are bridged to native workers; unsupported fork and namespace imports retain
test-failure endpoints. They never report success. Unimplemented syscalls
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
the complete thread suite, regular files and file-backed mappings remain open.

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

At `7b62337`, the 19-case NDK futex caller passes in both signed startup modes
and through both Bionic Linux syscall profiles. Source/object hashes and signed
containers were downloaded and verified. The normal startup plus both callers
measures 12.502 ms; forced GWP sampling measures 12.687 ms. These include a timed
wait, native condition-variable scheduling and trace output, not allocator-only
throughput. The regression IPA is still the M1 app; the newer Bionic frameworks
are separate signed artifacts.

The pthread extension uses real Bionic pthread_create/join/exit,
condition variables, mutexes, errno and key destructors in four joinable and two
detached workers. Each worker performs 32 malloc/realloc/free iterations. A native
reaper must complete all six workers, including deferred detached-stack teardown.
At `e828dad`, all six real Bionic workers pass in normal and forced GWP-ASan
processes. All 16 portable contracts pass, including actual supplied POSIX stacks.
[Host/Linux CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34816736646) and
the [iOS regression build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34816736632)
are green. Downloaded source/object hashes, both signed framework pairs and the
regression IPA are verified. Native setup/startup/clients/reaping takes 10.571 ms
in the normal process and 13.748 ms with forced sampling, including tracing and
timed waits. Reserved VA returns to 8,864,874,496 and 8,866,004,992 bytes respectively
after all workers are reaped. No physical device execution is claimed.

The first pthread runs (`e191021`, `b21d1c3`) created real guest workers, exercised
condition wakeups and joined them, then reported the worker's post-allocation
errno assertion. That assertion incorrectly required successful allocation to
preserve errno; Scudo's internal unsupported VMA-name call can change it. The
[errno contract](https://pubs.opengroup.org/onlinepubs/9699919799/functions/errno.html)
does not promise preservation after arbitrary successful library calls. The
fixture now tests distinct errno values across pthread contention and distinct
errno addresses across live workers, while retaining every allocation check.
VMA naming remains unsupported and continues to return ENOSYS.

At `cc6b074`, guest signal-mask inheritance and isolation pass in all six workers;
the separate syscall slice passes its expanded 53-case caller on signed macOS
and both Linux profiles. [Host/Linux](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34817253683)
and [iOS](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34817253654) are green.

The next report adds Darwin getrusage process peak RSS (bytes for the entire host
process, including the harness/libraries) and a separate pthread-client interval
through complete reaping. It also counts guarded malloc results per worker and
requires at least one in every worker in the forced-sampling process. These are
correctness-run measurements, not isolated allocator or iPhone performance.
