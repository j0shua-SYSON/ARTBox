# Loader services for ART

ART's pinned `sigchainlib/sigchain.cc` opens `libc.so` and resolves its signal
functions through that handle. Other runtime paths use `dladdr` and
`dl_iterate_phdr`. These calls must describe the Android libraries already
registered in the signed load group. Passing their names or handles to Apple's
dynamic linker would search a different ABI and return incorrect metadata.

The portable load-group queries now provide:

- A named module's breadth-first dependency lookup, including cycles.
- Lookup after the module containing a caller address within the fixed scope.
- Reachable image metadata in scope order, with ELF program headers and TLS IDs.
- Address-to-image and containing-symbol lookup, excluding unmapped gaps and
  undefined, absolute, TLS and zero-size symbols.

These queries allocate no memory and expose borrowed metadata. Their owner
keeps the group and signed images alive and serializes any mutation. Failed
queries preserve output values; a successfully resolved absolute symbol may
have address zero. Unreachable manifest entries cannot participate in a lookup.

The behavior follows the relevant dependency traversal and address rules in
the pinned Bionic `linker/linker.cpp` and `linker/linker_soinfo.cpp`. It covers a
single startup group. Namespace search, global-scope promotion, on-demand
loading, unloading and per-thread TLS symbol addresses are not implemented by
these queries. The Android `dlfcn` bridge layers guest-visible storage and
callback marshalling over this fixed scope.

Portable tests cover scope isolation, weak-symbol order, cycle termination,
caller selection, TLS IDs, symbol boundaries and missing/invalid inputs. All
25 local host tests pass. This is loader infrastructure, not Apple ART startup.
The signed math runner also checks these queries against its actual client and
libm images, including library-handle isolation and an address inside `sin`.
That native check passes at `1239be6`; the downloaded runner source, library
bytes and signed framework layouts verify against the same revision.

Before implementing the guest ABI, a separate fixture establishes 32 Linux
loader results and six interleaved thread-error checks. It covers explicit and
default handles, `RTLD_NEXT`, missing names, exact-path rejection, `dladdr`,
program-header callbacks with reentry and early termination, and balanced
repeated opens. The same original caller compiles for Android ARM64. Native
Linux execution passes at `6ec73bf`; downloaded source and binary hashes and the
execution log verify. This reference does not test a guest loader bridge.

The portable `artbox/dlfcn.h` context now owns loader handles and copied path
aliases. All libraries remain pinned by the startup group; closing balances an
open reference without unloading code. A handle belongs to one context and must
be open for explicit symbol lookup. Default and next lookups retain the group's
defined scope. The owner supplies a separate zero-initialized error record for
each thread, with read-and-clear behavior and preservation of unread errors
across successful calls. Local tests cover concurrent opens, invalid flags,
missing libraries, exact aliases, zero-valued symbols and cross-context handles.
The guest bridge now validates strings and output buffers through the shared
mapper. It places names in read-only guest pages, retains each thread's error
buffer, and exposes ELF headers and symbol names from the signed image mappings.
`Dl_info` and `dl_phdr_info` use explicit Android LP64 layouts. Program-header
callbacks must belong to signed executable segments and run without a mapper
lock; reentry and early termination are tested. TLS metadata requires an
owner-supplied lookup of the calling thread's existing TLS block.

The unchanged pinned AOSP `libdl.cpp` supplies seven entry points, including
caller-address capture for `RTLD_NEXT`. A thin native binding connects its
explicit `__loader_*` imports using host TLS. The original 32-case fixture and
six thread-error checks now have a signed Mac runner and iOS 15 framework
packaging in CI. Guest threads use VM-owned guarded stacks, which remain live
until join. Local portable tests and Android compilation pass; signed execution
of this bridge is pending. This does not establish Apple ART startup.
