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

The expanded fixture also links the pinned AOSP baseline memory/string objects
and their independent scalar oracle. It must pass 35,908 cases in host-created
guarded mappings through the same precompiled seven-word bridge. The same NDK
object runs on native Linux. This checks actual signed guest string code;
host mappings isolate overreads/writes, and no host libc function supplies the
expected results. The report times these checks separately from syscall loops.
The framework retains the complete Arm routines license before signing.

The same wrapper is also built and signature-verified for arm64 iOS 15. Each
framework includes Bionic's complete reviewed notice. The framework is a separate
CI artifact; it is not yet embedded in the app's M1 IPA. Apple linking and native
execution pass for this implementation. Windows checks the
controlled ELF, malformed layouts and synthetic final Mach-O failures.

At implementation `9e4f0490581a388dcdd2661cd9cb1bd4764b0066`, the
[host run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34752207898)
passed native macOS execution: 100 iterations, 200 writes, one constructor,
two PLT bindings and two RELR writes. Both linked/signed wrappers preserve
49,152 RX bytes followed by 1,392 RW bytes, including 1,032 zero-filled BSS bytes.
Their checked Mach-O addresses are 16,384 and 65,536 before the ASLR slide.
The mapped immutable ELF bytes also match before and after execution.

In this single run, loading plus byte validation took 1.405 ms, relocation plus
the probe constructor took 12 microseconds, and guest setup plus all 100
iterations took 0.496 ms. The original `iterations_ns` field includes guest
setup; subsequent reports time setup separately. These are fixture measurements,
not complete Bionic startup or per-syscall benchmarks. The signed Mach-O binaries
are 101,040 bytes on macOS and 101,056 bytes for iOS; the framework also carries
the 268,209-byte Bionic notice and bundle/signature metadata.

The downloaded iOS framework binary has SHA-256
`1a8941fc28447b1bd99990e5a1faf1014064417ff2a1325460a5230d91131189`.
Its layout, bytes and notice were rechecked against the downloaded ELF and
report. The [M1 iOS regression run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34752207900)
also passed; its IPA at `artifacts/m2-9e4f049/ios/ARTBox.ipa` has SHA-256
`53082e5c0416beda4c838375a486872c96cbb5bd90041ea487cdde5e40b8288d`.
The tested merge `8fffebf0382c642b06955be7df1ac6ed99a1cdff` includes the
implementation commit. That IPA still runs the M1 app, not this dynamic fixture.

The expanded string fixture at `3f69405083b12e386bd7ea9d5b9fb9eb9b9ac65c`
passes [native CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34753272777).
It preserves 65,536 RX bytes followed by 1,632 RW bytes, binds 32 PLT entries
and applies two RELR writes. All 35,908 string cases pass with 16 KiB guarded
pages on macOS and 4 KiB pages on Linux. The macOS checks take 429.852 ms,
including the scalar oracle and repeated buffer initialization; this is not
a libc throughput benchmark. Separately, loading/validation takes 2.546 ms,
relocation/constructor 40 microseconds, guest setup 14 microseconds and the
100 syscall loops 387 microseconds in that run. Hardware, page sizes and test
overhead prevent treating the Linux/Mac elapsed times as a platform speed ratio.

The iOS fixture binary is 117,568 bytes, SHA-256
`04070bc6414181ca7f22f8b9f0418b3498324f2b3e638bb4c694e919912238fc`.
Downloaded bytes, load layout and both notices match the report. The M1 IPA
at `artifacts/m2-3f69405/ios/ARTBox.ipa` has SHA-256
`50acee974d1e99d1e68c6aa172faba9d20726e4961ab96fd7a417c5a84ae25b5`;
tested merge `b351157fbf407eb8da0b8fd03fe588f9ed400287` includes the implementation.
The dynamic fixture remains separate from that IPA.

Still required: complete Bionic linkage/startup, guest TLS and thread ownership,
symbol versions/dependencies, general constructor ordering, RELRO protection,
broader Linux semantics and integration in the iOS app. No physical iPhone
execution is claimed.
