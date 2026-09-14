# Rooted files and virtual devices

The portable VFS owns one guest descriptor table for both regular files and
virtual devices. Host descriptors are opaque provider handles. The older
device-only interface is a facade over this table and retains its tests.
The initial cwd is `/`; absolute paths ignore dirfd, and relative paths use a
pinned directory handle. Parent components are rejected as a confinement policy.
`/system` is read-only. Other paths resolve below the provider's preopened root.

A small POSIX provider opens each path component relative to an already-open
directory, with NOFOLLOW at every step. It supports regular files and directories;
symlinks and other inode kinds are unsupported. Guest paths never become native
absolute paths. Open uses NONBLOCK internally so an unsupported FIFO cannot hang
before type inspection. This flag has no effect on regular-file I/O. Windows has
no provider yet and returns ENOTSUP; all portable table contracts run there using
an injected memory filesystem. macOS/Linux CI also run the same contract against
real disk files rooted in disposable fixtures.

The implemented calls are openat, read, write, lseek (SET/CUR/END), fstat,
newfstatat, faccessat and close. Supported flags include read/write access,
create/exclusive/truncate/append, directory and close-on-exec. Unknown flags fail
explicitly. Metadata is encoded into Linux ARM64's 128-byte stat layout; exposed
backing files belong to guest UID/GID 10000, retaining their permission bits.
The native process umask also constrains newly created file permissions. Access
checks evaluate the exposed owner bits; multiple guest users are not modeled.

A table mutex serializes descriptor changes and shared file offsets. The mapper
validates each page-sized I/O fragment and holds its lock while the provider
reads/writes that range. A bad read destination therefore cannot consume file
bytes before EFAULT. Later inaccessible pages yield partial progress. EOF copies
no bytes; an unmapped destination at EOF returns zero after a non-mutating
position/size check. The same edge case is checked through real Linux syscalls.
Zero-length regular-file I/O does not inspect its buffer. General pipes/sockets,
file-backed mappings, dup/fcntl, directory enumeration and mutable directory
operations are not implemented by this stage.

The local contract covers mixed descriptor allocation, create/exclusive/truncate,
relative directory paths, read-only system files, symlink/traversal rejection,
seek/stat, EOF, inaccessible-buffer offsets, partial I/O and 512 concurrent
appends. It also checks that a symlink target outside the guest root remains
unchanged. At `9cf76a1`, all 17 contracts pass on Windows, macOS and Linux;
macOS/Linux use the actual rooted POSIX provider as well as the injected provider.
The Linux branch also compares real syscall behavior at EOF and EFAULT. The
[host/Linux run](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34818958455) and
[iOS build](https://github.com/j0shua-SYSON/ARTBox/actions/runs/34818958365) are green.

The next signed integration adds an identical 41-case NDK file caller paired
with the original/adapted Bionic syscall entries on Linux ARM64. Its portable
execution passes locally. The real Bionic pthread client also creates one regular
file per worker, writes 32 records, checks fstat/seek, reads each record back and
closes it. The signed process now owns a rooted file provider; each normal/sampled
process receives a fresh root retained with its diagnostic artifacts. This new
signed execution still awaits CI.

The first ARM64 Linux file oracle rejected directory opens: the initial table
used the generic Linux flag layout. ARM64 uses `O_DIRECTORY=0x4000`,
`O_NOFOLLOW=0x8000`, `O_DIRECT=0x10000`, and `O_LARGEFILE=0x20000`.
The core and native provider now use these guest values; the NDK caller asserts
them against its target headers. Direct I/O remains explicitly unsupported.

## Native data mappings

`artbox_vfs_mmap` acquires an independent reference to a regular file under the
descriptor lock. The VM owns that reference through partial unmap and until its
reservation is released. POSIX uses a duplicated close-on-exec descriptor and
native `MAP_PRIVATE`/`MAP_SHARED` views inside mapper-owned reservations.
Closing the guest descriptor does not invalidate those views. No mapping may
become executable. Private writable maps may use a read-only descriptor;
shared read-only maps retain a write-protection ceiling across `mprotect`.

`MADV_DONTNEED` remaps file pages from the retained file, dropping private copies
and preserving shared file data. Anonymous replacements inside a file reservation
are tracked separately and still discard to zero. `msync(MS_SYNC)` writes back
shared pages only. ASYNC and flags=0 perform validation without writeback, matching
[Linux's implementation](https://github.com/torvalds/linux/blob/v6.12/mm/msync.c).
Guest memory locking is unsupported, so INVALIDATE has no locked-range behavior.

Limits: file `MAP_FIXED`, huge pages, device mappings, cross-reservation sync,
and partial sync across holes remain unsupported. Native EOF/truncation faults
are not translated into recoverable guest signals. The Windows native file
provider remains unsupported; portable ownership tests use an injected backing.
The 43-case NDK mapping caller is paired with actual Linux ARM64 and the signed
Bionic client. Its first native CI run is pending; local ownership tests pass.

At `ce6c6eb` the original 41-case file caller and six Bionic workers' file round
trips pass signed macOS and both Linux profiles. The raw caller takes 0.457 ms
normally and 0.377 ms under forced sampling in single traced correctness runs.
