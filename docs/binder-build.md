# AOSP Binder build

`scripts/build_binder.py` compiles Android 15's real kernel-IPC libbinder, the
eight libutils Binder support units, Looper/Timers and nine platform support
units for `aarch64-linux-android35`. Its six static
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

The compiler reserves Apple's x18 and ARTBox's x27/x28 registers, uses baseline
ARMv8 without outlined atomics, and disables exceptions/RTTI as in the AOSP C++ profile. RefBase callstack
diagnostics use AOSP's `ANDROID_UTILS_CALLSTACK_ENABLED=0` option, avoiding an
unwinder dependency in this support library; reference counting is retained.
Android API 35 describes the guest declarations, independently of iOS 15's
deployment target. No generated code is executed during this cross-build.

The build checks the generator's pinned compiler/source identity, exact generated
file inventory and hashes, each object's ELF64 little-endian ARM64 relocatable
header, executable-section size against disassembly, forbidden instructions, and
exact archive membership. Empty conditional units are accepted only when their
ELF executable sections also contain zero bytes. Artifacts retain source and object hashes,
compiler version/options, unresolved imports and a deterministic corresponding
source ZIP. Unit controls reject changed generation settings, changed/missing/
extra files and accidental host/shared-library objects. Native compilation is a
required CI job, separate from Binder protocol and signed ART runtime tests.

`BufferedTextOutput.cpp` has a nontrivial C++ `thread_local` object and its
initialization guard. Its original ARM64 object reads `TPIDR_EL0`; it cannot
execute unchanged alongside Darwin TLS. The build applies the existing ART
global-dynamic TLS adapter to this exact source hash and the two declared TLS
symbols, preserving four descriptor calls while replacing the one thread-pointer
read with the absolute-descriptor zero base. It verifies that compiler assembly
reproduces the original object, retains both objects and assemblies, and rejects
changed access counts or any remaining forbidden instruction. No upstream C++
source, initialization guard or destructor registration is removed. The eventual
link must use `--no-relax`, and the runtime must register this additional ELF TLS
module. Buffered logging and its thread-exit destructors still require signed
execution acceptance; this archive build does not establish that behavior.

Function/data sections permit the eventual link to discard unused routines,
while `symbols.json` continues to describe every archive member's imports.

The platform support archives contain the original libutils Thread, system-clock
and property-callback implementations, libcutils native-handle, multiuser,
property, ashmem and tracing code, and libvndksupport's loader calls. They use the
Android source branches and retain real dependencies on Bionic and the loader.
In particular, tracing attempts to open its trace marker and handles an absent
file through AOSP's error path; no pretend trace sink is supplied. Ashmem and
vendor namespace APIs are not thereby implemented by ARTBox.

The Binder-only macro disabling implicit `unique_fd` conversion is undefined
for `ashmem-dev.cpp`, matching libcutils' build profile and preserving its RAII
ownership. `trace-dev.cpp` keeps C++20 and the original atomic initialization;
the NDK's `ATOMIC_VAR_INIT` deprecation warning remains visible but nonfatal for
that unit. The pinned Soong configuration likewise makes deprecated diagnostics
nonfatal. Other warnings and all instruction checks still fail the build.
These archives require subsequent runtime tests for threading, properties,
tracing and teardown before service acceptance.

The original `libutils/Looper.cpp` and `libutils/Timers.cpp` are pinned at the
same Android 15 tag. Adding them brought the build to 51 objects; the platform
support selection above brings it to 60 objects across six archives. Looper uses the already implemented
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
At `480238f`, this CI step passes all 43 assertions and both negative controls
on native ARM64 macOS through signed Bionic. Downloaded execution logs, source
and binary hashes, both framework layouts and worker cleanup verify independently.
The normal fixture takes 14.976 ms in that correctness run. This establishes
signed guest Looper execution; Binder attachment and servicemanager execution
remain ahead, and physical iPhone execution is unverified.

## VINTF and original APEX parser generation

The real Android servicemanager depends on VINTF and the APEX XML API. Build
these dependencies separately so the focused Binder/Looper build remains usable:

```sh
python3 -B scripts/xsdc.py --build-dir build/m4/xsdc
python3 -B scripts/build_vintf.py --xsdc-dir build/m4/xsdc --jobs 2
```

XSdc is compiled from its 39 unchanged Java units using a pinned portable JDK
and the pinned original Commons CLI 1.2 dependency. `--java-home` can select an
existing JDK 17 or newer. The original Android APEX schema and Soong options
produce two C++ files and two headers. Each build generates twice in fresh
directories, verifies exact reviewed hashes, rejects malformed schema and an
unknown root, and retains original and LF-normalized output. Line-ending
normalization accounts for Java's platform-specific `println`; no parser logic
is edited. All three host platforms must reproduce the same four canonical
files in CI. Unit controls reject substituted, missing and extra output, changed
options, incomplete generation evidence and redirected output paths.

`build_vintf.py` keeps `LIBVINTF_TARGET` and the Android source branches. Its four
archives contain 25 original VINTF units plus the two generated units, two
libkernelconfigs units, three libkver units and one TinyXML2 unit: 33 objects.
It reuses Binder's ARM64 executable-section/instruction audit, reserves x18/x27/x28,
checks deterministic archive membership and reports all unresolved imports.
Its source artifact includes the schema/compiler source archive and all compile
inputs and notices. Pass `--ndk-root`, `--build-dir` and `--jobs` to configure the
build; omit `--xsdc-dir` to generate the parser locally first.

Use quote-only lookup for VINTF's private headers so `Regex.h` cannot shadow
the NDK's `<regex.h>` on case-insensitive filesystems. Original deprecated,
inconsistent-override and sign-comparison diagnostics remain visible and
nonfatal, consistent with the pinned Soong warning policy. Only the original
KernelConfigs stack VLA and parse_string's unused constants receive their
respective additional nonfatal diagnostic flags. No source workaround, host-only
runtime profile or replacement kernel implementation is introduced.

These are compile and code-generation checks. They do not demonstrate parser
execution, complete XSD validation, VINTF access to a device, SELinux behavior,
or signed servicemanager registration. A private full-service link currently
exposes eight unresolved symbols: two Access methods, three libc++ regex helpers,
Bionic regcomp/regfree and security_policyvers. Adding the four original Bionic
regex units and one original libc++ regex member in the private probe leaves
three policy imports. That probe establishes neither signed execution nor a
complete access-policy implementation.

The public Bionic selection now includes the original regex units. Its shared
NDK caller has 120 checks for the LP64 ABI, BRE/ERE, matching/captures, flags,
bounded embedded-NUL input, errors and allocation/free. Removing newline
semantics or truncating the capture count must fail at the specified checks.
`test_bionic_startup.py` links that exact caller into both signed profiles and
builds a static Linux ARM64 reference from the same original Bionic sources.
`test_binder_libc.py` verifies its source/object identity and compares native
execution with both signed reports. The existing 50 libc checks and their two
controls remain separate. Both signed Mac profiles and the Linux reference
pass; the normal profile's 120 regex checks also pass on the test iPhone at
`31c9a4a`. Binder/service execution still requires its own acceptance tests.

### Shared kernel-config parser diagnostic

`build_kernel_config_check.py` compiles the original `KernelConfigParser.cpp`
and one MIT caller, then extracts the hash-pinned NDK `regex.cpp.o` member.
All selected objects pass the Android executable boundary audit. The same
caller/parser objects link into a static Linux ARM64 reference and the signed
Apple test image. Linux uses the original NDK libc/STL; Apple resolves the
remaining C++ imports from the existing signed ART bootstrap dependency.

The fixture checks byte-at-a-time input, whitespace and trailing comments,
unset keys, duplicate rejection with retained values, recovery after a bad
line and final input without a newline. It reports 89 successful assertions.
Disabling unset-comment processing must return -104; disabling relaxed parsing
must return -105. Neither nominal success nor a different failure is accepted.

`test_kernel_config_reference.py` executes the exact producer binary on native
Linux ARM64. `test_kernel_config_guest.py` requires that reference's matching
revision, object, binary and source hashes before packaging the fifth signed
image after libc, libart, libm and libdl. The host runner checks all three
results, pre-start ART/sigchain behavior and complete cleanup without creating
a JavaVM. Frameworks target macOS ARM64 and iOS 15 with empty entitlements;
only Mac execution is part of this gate. Build, reference and guest artifacts
retain notices, command logs, corresponding source and separate execution flags.

At `136a087`, native Linux ARM64 and signed Mac execution both pass 89/-104/-105.
Independent artifact verification checks four object hashes, exact source and
reference identities, two framework layouts and ten notice hashes. The signed
run initializes 33 constructors across five images, verifies pre-start heap and
sigchain behavior, and cleans up with no workers or JavaVM. Its parser calls
take 0.153 ms; load/relocation takes 17.834 ms and bootstrap takes 1.069 ms.
The Linux executable's complete process takes 1.055 ms, a different measurement
scope. These are diagnostic observations, not a sustained benchmark.

This focused check does not establish general VINTF device/kernel metadata
support or execute servicemanager; policy and process-isolation work stays open.
The iOS framework layout and signature are verified, but this parser fixture
has not executed on a physical iPhone.

### Service access policy

The portable `artbox_service_policy` stores up to 1024 immutable, copied grants
for exact calling PID/UID, operation and service name. Unconfigured identities,
root without a grant, unknown names and unknown or combined query operations
are denied. List permission is explicit and separate from find/add. Names and
credentials cannot change by modifying the original configuration storage.
The Windows contract passes malformed/duplicate configuration, name boundaries,
identity substitution and concurrent query checks. At `84bfde3`, all 23 host
jobs and iOS pass, and its ART IPA independently verifies. The Access adapter's
signed execution remains pending; this is not yet a running service or an
implementation of SELinux.

`build_servicemanager.py` compiles unchanged original main/ServiceManager and
five ARTBox policy/contract units into an audited archive. The corresponding
source includes every selected upstream byte, notice, generated AIDL input and
the original ID header supplied at the service's expected include path.
Original Access.cpp is not compiled. The adapter uses real IPCThreadState
identity; expected contract results are 20/-109/-107 in separate processes.
The VINTF policy-version contract expects five checks, including preservation
of its previous value when the unsupported fetch is logged. These contracts
are build inputs; their runtime acceptance and the three-role service test
remain pending.
