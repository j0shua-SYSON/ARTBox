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
sizes from one byte to one MiB: 134 checks. This fixture does not yet create guest
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
pair. Native execution is pending the first CI run. Diagnostics and source,
object, framework and notice hashes are retained in the `bionic-startup` artifact.
