# Controlled dynamic wrapper

This M2 prototype links the actual NDK-built Bionic syscall entries and errno
setter into a shared ELF. The prefixed object is identical to the Linux syscall
oracle input. An original probe adds a constructor, a relocated constant pointer
and zero-filled state. It does not include complete libc startup, Bionic's errno
storage, pthreads or the allocators; it cannot satisfy M2 acceptance by itself.

The link script produces one RX load beginning at ELF address zero and one
16 KiB-aligned RW load. `tools/wrap_dynamic.py` copies immutable bytes and gap
padding into `__TEXT,__artbox`, and writable bytes plus zero-filled BSS into
`__DATA,__artbox`. Apple tools link the assembly wrapper. The verifier checks
both byte hashes, section bounds, permissions and the exact distance between
the mapped sections before and after signing. A shifted RW section is an error;
no instruction is rewritten to compensate. Materialized BSS and page padding
increase file size. This controlled layout is not an arbitrary ELF converter.

The macOS test opens the signed framework, compares mapped initial bytes with
the original ELF and verifies one load bias. The existing portable relocation
engine binds two explicit host imports and applies RELA/RELR writes only to
the RW section. A bounded constructor-array entry is called through a precompiled
seven-word register bridge. The probe checks its relocated pointer, zero-filled
state and exactly one constructor invocation.

The Bionic generic syscall entry then reaches the existing five-syscall
translator. The test checks write from signed constants and mapped memory,
mmap/protection/unmap, guest exit, unsupported calls, and Bionic's real errno
conversion against separate test errno storage. It requires 100 iterations and
200 successful writes and compares immutable bytes again afterward. This is a
fixture harness, not a complete dynamic-library namespace or thread runtime.

The same wrapper is also built and signature-verified for arm64 iOS 15. Each
framework includes Bionic's complete reviewed notice. The framework is a separate
CI artifact; it is not yet embedded in the app's M1 IPA. Apple linking and native
execution results are pending CI for this implementation. Windows checks the
controlled ELF, malformed layouts and synthetic final Mach-O failures.

Still required: complete Bionic linkage/startup, guest TLS and thread ownership,
symbol versions/dependencies, general constructor ordering, RELRO protection,
broader Linux semantics and integration in the iOS app. No physical iPhone
execution is claimed.
