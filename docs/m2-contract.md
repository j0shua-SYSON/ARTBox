# M2 dynamic Bionic contract

M2's [automated acceptance](acceptance/m2.md) passes. The area contract below
precedes Bionic compatibility changes; the integration denominator's later
freeze is documented separately. Physical device checks remain optional under
ADR 0004.

## Source and native execution

Build real AOSP Bionic from `android-15.0.0_r1`, commit
`361ba86734fb2821a6adcfdf775db8abd04e0de0`. The source archive and libc notice are
hashed in `third_party/sources.json`. The NDK's API link stubs are not a runtime
libc. No replacement libc may be reported as Bionic acceptance.

Keep the Android ARM64 ELF artifacts as the inputs to build-time packaging.
Continue with the M1 Apple-linker wrapper, extending it for multiple segments,
exports and writable relocation targets. Signed text must retain its validated
address relationships. Reject runtime text fixups and executable anonymous
mappings. Resolve guest dependencies through an explicit bundle manifest;
guest symbol lookup must not accidentally bind to Darwin libc.

The portable loader must handle PT_LOAD, dynamic tables, RELA, RELR, GNU hash,
DT_NEEDED, TLS templates and constructors. Tests must include stripped section
tables, malformed bounds and hash chains, unsupported relocation types, symbol
versions, weak/strong resolution, dependency cycles, and attempts to relocate
text. Parsing ELF data is not evidence that its code executes.

## Bionic adaptation points

Bionic's AArch64 TLS getter reads `tpidr_el0`, and its pthread implementation
builds a Bionic TCB and stack before `clone`. Source-level bridges must provide
guest TLS per native host thread and preserve the host's reserved registers.
Neither Bionic nor guest code may change Darwin's thread pointer. Patch syscall
stubs before signing and inventory handwritten assembly and inline syscalls.

The upstream shared libc also depends on loader interfaces and allocator/runtime
components. Record each actual build dependency and its license before import.
Keep Bionic's public semantics and source implementations wherever possible;
document every replaced kernel/platform boundary and every unsupported feature.

## Acceptance suite fixed before compatibility work

The suite is an Android NDK ELF linked dynamically to the built Bionic. Run the
same test sources against native Linux/Android semantics as a paired oracle.
Every test contributes to a published denominator, including failures and
unsupported cases. Do not delete or reclassify a failing test to reach 90%.

| Area | Required cases |
| --- | --- |
| Dynamic startup | Bionic identity/provenance, dependency resolution, exported symbol lookup, constructor order and exactly-once initialization |
| Threads | Create/join, return values, concurrent work, thread-local errno, TLS isolation, mutex contention, condition wait/signal/broadcast, timed waits, thread cleanup |
| Files | Rooted openat, create/truncate/exclusive flags, read/write/short I/O, seek, fstat layout and values, close/bad descriptors, relative paths, traversal rejection, proc/dev fixtures |
| Memory | Anonymous zero fill, rounding, read/write access, partial protection/unmap, concurrent mappings, file-backed mappings, cleanup, invalid flags/addresses, executable-map rejection |
| Error contracts | Linux negative errno at the syscall boundary and Bionic errno/return conventions at the public API boundary |

The ordinary thread, file and memory success cases and Bionic identity are
mandatory, even if a larger denominator would hide their failure. M2 requires
at least 90% overall, native ARM64 macOS execution, portable host tests, and a
signed iOS 15 build containing the same suite and libraries. Report timings,
resident memory, pass/fail counts and the concrete remaining incompatibilities.
M3 starts only after M2's automated acceptance is green.

## Frozen integration denominator (version 1)

`fixtures/bionic-startup/acceptance.json` fixes **328 acceptance expectations**:
146 allocator/device, 19 futex, 41 regular-file, 43 file-mapping, 35 anonymous
memory, 18 pthread-timeout, 22 proc-commandline, plus four complete workload
contracts (pthread lifecycle/concurrency, ELF TLS, symbol versions, and startup
constructors). Repeated loop iterations, synchronization retries and internal
assertions do not inflate those four workload counts. Each workload must finish
all its assertions to contribute its one pass. Both normal and forced-sampling
processes must pass; they are separate runs of the same denominator.

The area requirements above predate compatibility work. This numeric integration
manifest is frozen after the existing allocator/file/TLS work and before the
new proc implementation and final M2 acceptance integration. It does not claim
the exact numerical denominator was fixed before every earlier M2 change.
Failures and unexecuted cases remain in the denominator; an aborted process
cannot produce a passing acceptance record. No failing case may be removed or
reclassified to reach the threshold. All current groups are mandatory, a
stricter gate than the minimum 90 percent.

The proc fixture specifies a read-only snapshot of initial argv bytes at
`/proc/self/cmdline`, virtual proc directories, zero stat size, independent open
offsets, EOF, partial reads, rewind/SEEK_END and errno behavior. It does not cover
mutating argv/setproctitle, `/proc/self/maps`, readlink or arbitrary process IDs.
The Linux kernel reads live argument memory; that mutation behavior is a known
limitation of the first snapshot path. Test the raw caller against native Linux
before adding the proc compatibility implementation.
