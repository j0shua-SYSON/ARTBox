# AOSP Binder build

`scripts/build_binder.py` compiles Android 15's real kernel-IPC libbinder and the
eight libutils Binder support units for `aarch64-linux-android35`. Its two static
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
