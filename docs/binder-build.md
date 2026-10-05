# AOSP Binder build

`scripts/build_binder.py` compiles Android 15's real kernel-IPC libbinder, the
eight libutils Binder support units, and Looper/Timers for `aarch64-linux-android35`. Its three static
archives are intermediate ELF objects for the signed native packaging pipeline.
They are not executable iOS libraries. Linking, signed attachment and real
servicemanager registration/ping/death acceptance remain required for M4.

Generate the bindings on a supported native Mac/Linux host first:

```sh
python3 -B scripts/binder_aidl.py --output build/m4/aidl
python3 -B scripts/build_binder.py --aidl-dir build/m4/aidl --jobs 2
```

Windows can compile the same objects using the generated artifact from CI.
Pass its profile directory, containing `generation.json` and `generated/`, as
`--aidl-dir`. No foreign host compiler is executed. `--ndk-root`, `--build-dir`
and the common environment configuration control tool and output locations.

The source graph includes 36 libbinder units and five generated service units.
It keeps `BINDER_WITH_KERNEL_IPC`, the Android build branches and the native
handle/blob implementations. It does not select the RPC-only SDK profile or
define recovery/vendor modes to avoid dependencies. The import inventory in
`symbols.json` deliberately retains every unresolved external symbol; an archive
build is not a successful link. This exposes the remaining libcutils, libutils,
APEX, logging, libc++ and Bionic dependencies before runtime integration.

Sources and headers are exact file selections at named AOSP tags, with SHA-256,
Git blob identities, original notices and upstream build metadata. The extra
graphics headers are transitive dependencies of `utils/ThreadDefs.h`; compiling
them does not introduce a graphics backend. The APEX header declares the real
API; this build supplies no dummy APEX implementation. Soong's original
`cc/config/global.go` is pinned as the reference for warning settings, including
the still-visible unused-but-set-variable warning in `IServiceManager.cpp`.

The compiler reserves Apple's x18 register, uses baseline ARMv8 without outlined
atomics, and disables exceptions/RTTI as in the AOSP C++ profile. RefBase callstack
diagnostics use AOSP's `ANDROID_UTILS_CALLSTACK_ENABLED=0` option, avoiding an
unwinder dependency in this support library; reference counting is retained.
Android API 35 describes the guest declarations, independently of iOS 15's
deployment target. No generated code is executed during this cross-build.

The build checks the generator's pinned compiler/source identity, exact generated
file inventory and hashes, each object's ELF64 little-endian ARM64 relocatable
header, and exact archive membership. Artifacts retain source and object hashes,
compiler version/options, unresolved imports and a deterministic corresponding
source ZIP. Unit controls reject changed generation settings, changed/missing/
extra files and accidental host/shared-library objects. Native compilation is a
required CI job, separate from Binder protocol and signed ART runtime tests.

The next service dependency is the original `libutils/Looper.cpp` and
`libutils/Timers.cpp`, pinned at the same Android 15 tag. The Binder build now
contains 51 objects across three archives. Looper uses the already implemented
eventfd/epoll boundary; servicemanager's periodic callback also needs timerfd.
No replacement Looper or private event-loop API is supplied.

The unchanged `Timers.cpp` compares a nonnegative clock selector with a `size_t`
bound. The pinned Soong `global.go` disables `-Wsign-compare` globally; this build
instead keeps the diagnostic visible and makes it nonfatal only for that unit.
The per-unit flag is recorded in the graph and build report. The original test
caller retains warnings-as-errors, and no runtime assertion is disabled.

`scripts/test_aosp_looper.py --profile linux` builds and runs an original MIT
caller against these unchanged AOSP sources, their real RefBase/Vector support,
and AOSP's host logging implementation on native Linux ARM64 or x86_64. The 43
assertions cover per-thread Looper ownership, coalesced and cross-thread wake,
callback auto-removal, level readiness, descriptor replacement/removal, ordered
and delayed messages, cancellation, and a timerfd callback. Negative controls
retain a callback and omit a wake; both must fail at their specific assertions.
The worker observes Looper's `isPolling()` publication. This checks the wake
protocol without claiming it observes entry into the kernel's blocked syscall.

`--profile android` builds the same caller and ten upstream units into an ARM64
archive, preserving imports for the existing signed Bionic/C++/logging closure.
Every object is checked for architecture and forbidden syscall/thread-pointer/
reserved-register instructions. This profile does not execute the guest yet.
It is preparation for the signed Bionic test, not servicemanager acceptance.

The shared Apple harness now has a separate five-image Looper diagnostic:
libc, libart, libm, libdl, then the Looper test library. `libart` supplies the
already-built C++ and logging code without starting a JavaVM. The selected
Bionic sources include the original public `eventfd()` wrapper, complementing
the existing translated syscall entry. The fixture uses Bionic pthreads and
TLS and reaches the portable VFS for its eventfd, epoll and timerfd operations.

On an ARM64 Mac, `scripts/test_looper_guest.py` consumes verified Android
objects and the four bootstrap dependencies from the same clean Git revision,
links with no undefined strong symbols, audits the final instruction boundary,
and packages ordinary signed macOS and iOS 15 frameworks. It executes each of
the three fixture modes in a fresh process, checks the exact Linux reference
results, and requires the Bionic worker to be reaped even after a deliberate
failure. It preserves binary/source hashes, notices and execution logs. The
shared ABI result header lives with the platform headers so every existing
Apple harness source archive also includes its complete compile-time inputs.
Signed execution remains unverified until this CI step passes; the Looper
framework alone does not establish servicemanager or physical iPhone support.
