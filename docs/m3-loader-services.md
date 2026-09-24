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
these queries. The Android `dlfcn` bridge still needs handle/error lifetime,
callback ABI, guest-visible metadata storage and native execution tests.

Portable tests cover scope isolation, weak-symbol order, cycle termination,
caller selection, TLS IDs, symbol boundaries and missing/invalid inputs. All
25 local host tests pass. This is loader infrastructure, not Apple ART startup.
