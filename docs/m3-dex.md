# M3 DEX loading evidence

This is a format-validation prerequisite. It does not start ART or execute a
DEX method, and it does not satisfy M3 acceptance.

- Implementation: `28b13ffa6a976b757359fb97926a87e3fc162b8b`.
- [Host, native Linux ARM64 and integrated iOS regression CI](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34841335288).
- [Independent iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34841335297).
- Downloaded evidence: `artifacts/m3-28b13ff/dex` and
  `artifacts/m3-28b13ff/linux`. Reports retain the tested merge, project sources,
  exact component selections, generated inputs and object hashes.

An original generator emits a 448-byte DEX 035 file containing
`artbox.Hello.message()Ljava/lang/String;`. Its instructions load the string
`hello from ARTBox ART` and return that object. AOSP's pinned `DexFileLoader`
opens it with structural and checksum verification enabled. The caller then
checks the class, method, signature, access flags and instruction data through
upstream accessors. This inspects the instructions; it does not invoke them.

The hello DEX SHA-256 is
`6d080e7780d6e0c1187bddf33c12dd22b6b59fc630a48eb62f639971b75f1258`.
Both native systems generate identical inputs and pass the same six cases:

| Input | AOSP loader result | Process exit |
| --- | --- | ---: |
| Original hello | Accepted, metadata matches | 0 |
| Duplicate map kind | Rejected | 2 |
| Method name beyond string table | Rejected | 2 |
| Overflowing instruction count | Rejected | 2 |
| Invalid checksum | Rejected | 2 |
| Truncated map | Rejected | 2 |

Every malformed input reaches AOSP's loader/verifier and produces its rejection
diagnostic. Every result explicitly records `dex_executed: false`. The Linux
comparison checks the Mac report's project revision, source pins, generated
input hashes and time-helper adaptation before running the cases.

Each native build compiles 48 implementation/generated/caller units. The Mac
executable is 216,336 bytes; the iOS framework binary is 225,776 bytes. Downloaded
objects, notices and binaries match the reports. ARM64 Mach-O inspection confirms
the macOS 11/iOS 15 deployment commands, ordinary ad-hoc signatures, empty
entitlements and no writable executable segment. CI also performs strict
signature verification. The framework is not yet embedded in an ART execution
entry or physically launched.

Windows separately compiles the 31 Android ART/generated/caller units with NDK
r28c at Android API 35. That establishes source compilation only. This baseline
keeps C++ objects inside each platform build and exports a C entry point; it
does not resolve Android C++ imports against the host's standard library.

Linux exposed two build portability gaps: AOSP's time helper included `<limits>`
after a header using it, and libbase's POSIX `strerror_r` wrapper needed an
explicit POSIX feature level in strict C++ mode. A checked include-order overlay
and `_POSIX_C_SOURCE=200809L` retain upstream behavior. See ADR 0033 and
[third-party notices](../THIRD_PARTY.md).

No isolated performance measurement was taken. Runtime startup, a matching boot
class path, managed allocation/collection, exceptions, JNI, instruction dispatch
and code-generation restrictions remain to be integrated and tested.
