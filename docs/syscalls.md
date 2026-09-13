# Linux syscall compatibility

No Linux syscall is implemented at M0. Host logging is not a guest syscall.

| Planned M1 syscall (AArch64 number) | Intended initial subset | Linux oracle / Darwin / device |
| --- | --- | --- |
| write (64) | Guest stdout/stderr with partial-write/error semantics | Not implemented / not tested |
| exit (93) | End a guest invocation with status; keep the app alive | Not implemented / not tested |
| mmap (222) | Anonymous private non-executable memory; explicit flag rejection | Not implemented / not tested |
| mprotect (226) | Non-executable permissions on owned guest mappings | Not implemented / not tested |
| munmap (215) | Validate alignment, bounds, mapping ownership and holes | Not implemented / not tested |

Before implementation, add tests that run against a real Linux host and
exercise the corresponding translator on macOS. Capture unsupported cases,
negative Linux errno values, 4 KiB/16 KiB page assumptions and pointer safety.
Never pass guest flags, errno values or structs straight to Darwin. No test
skip counts toward implemented coverage. Later calls follow observed demand.
