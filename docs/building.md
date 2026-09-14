# Building ARTBox

The portable core supports Windows, macOS, and Linux. The iOS app requires a
Mac with Xcode; CI builds a real arm64 device target with a minimum of iOS 15.
Native Android ARM64 execution experiments require an ARM64 host. Format and
portable-core tests run on other architectures without CPU emulation.

## Requirements

- Python 3.9 or newer, standard library only.
- CMake 3.24 or newer and a C11 compiler (MSVC, GCC, or Clang).
- A build backend supported by CMake. Ninja is optional.
- Xcode and the iOS SDK for `ios`; no Xcode generator package is needed.

Use existing tools or install them using your platform's preferred method.
ARTBox's scripts do not perform global installation or require a package manager.

## Commands

```sh
python scripts/build.py host
python scripts/build.py host --generator Ninja --jobs 8
python scripts/build.py host --build-dir "build/custom host" --artifacts-dir artifacts/custom
python3 scripts/build.py ios --with-guest --bundle-id org.example.ARTBox
python3 scripts/build.py ios --deployment-target 15.0
```

`--config` chooses the CMake build configuration (default `Release`).
`--cmake-arg=-DNAME=VALUE` forwards additional configure options and is repeatable.
Use a separate build directory when changing generators or compilers.

Direct CMake works without sourcing an environment script:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --config Release
ctest --test-dir build/host -C Release --output-on-failure
```

Out-of-source paths can be anywhere appropriate for the host, including a
separate disk or shared build volume. Set `CC` or `CMAKE_GENERATOR`, or pass a
CMake toolchain file, using ordinary CMake conventions.

## Native M1 experiment

```sh
python scripts/test_pack.py
python scripts/test_dynamic.py
python3 scripts/test_native.py
```

The first command builds a libc-free Android ARM64 ELF and checks both packaging
inputs on Windows, macOS or Linux. It downloads the pinned, relocatable NDK r28c
into the configured cache if needed. Set `ARTBOX_NDK_ROOT` or pass `--ndk-root`
to use an existing r28c installation. `--build-dir` selects the fixture directory.
The ELF manifest records the NDK/compiler versions, input paths and hashes.

`test_dynamic.py` builds NDK shared objects and compares the portable ELF view
with LLVM, including sectionless and malformed variants. It validates data
without executing guest instructions and requires a built portable host.

`test_native.py` requires an ARM64 Mac with Xcode. It links/signs both
frameworks, executes each 100 times on the native CPU and tests the same entry
used by the iOS app. CI separately runs the original ELF on native ARM64 Linux.
Neither the packer nor the portable host tests emulate an ARM64 CPU.

## Pinned sources for M2 development

```sh
python scripts/sources.py bionic
```

This optional development command requires an authenticated GitHub CLI (`gh`).
It verifies the archive and notice hashes in `third_party/sources.json` and
extracts the selected source into `ARTBOX_CACHE_DIR/sources`. Existing GitHub
credentials are used; CI can provide `GH_TOKEN` through its normal secret
environment. Original source archives stay in the configured download cache.
This fetch command does not build or claim a working Bionic runtime.

A source pin can combine an archive hash with an explicit file list. This
downloads the archive once and extracts only the pinned regular files, checking
each file before installing the source tree. Cached selections are revalidated
on reuse. This mode is useful for larger class-library source selections.

## Paths and environment

| Setting | Default | Command-line option |
| --- | --- | --- |
| `ARTBOX_BUILD_DIR` | `build/` under the checkout; command adds `host/` or `ios/` | `--build-dir` overrides the final target directory |
| `ARTBOX_CACHE_DIR` | `.env/` under the checkout | `--cache-dir` |
| `ARTBOX_TEMP_DIR` | `.tmp/` under the checkout | `--temp-dir` |
| `ARTBOX_ARTIFACTS_DIR` | `artifacts/` under the checkout | `--artifacts-dir` |
| `ARTBOX_ISOLATE_HOME` | unset, retain the user's home/configuration | Set to `1` for an isolated build home |

Relative overrides resolve against the invocation directory. The Python entry
point configures child-process scratch paths and supplies defaults for package
and compiler caches. Explicit cache/SDK variables, including `ANDROID_HOME`,
`CCACHE_DIR` and `PIP_CACHE_DIR`, take precedence over those defaults. Git identity,
GitHub authentication and signing configuration are inherited. No credentials
are copied or printed by environment setup.

These environment defaults control cooperative tools; they are not a filesystem
sandbox. Host-specific storage policy and diagnostics belong in ignored `work/`,
along with personal notes and local helper scripts. Do not commit credentials,
provisioning profiles, private keys, downloaded SDKs, APKs, or generated artifacts.

## iOS artifacts

`scripts/build.py ios` builds without automatic signing and then applies an
empty-entitlement ad-hoc transport signature. It verifies the bundle identifier,
deployment target, arm64 device load commands, segment permissions, signature,
entitlements, and portable startup symbol before packaging the IPA. The manifest
records source revision, working-tree state, Xcode/SDK versions and SHA-256.
Add `--with-guest` to build and embed both M1 frameworks. The build verifies their
signatures, exact hashes and iOS load commands, and writes `guest-bundles.json`
with the shared ELF hash, toolchain provenance and packaging measurements.

Generated Xcode files remain in the selected iOS build directory. Do not override
`CONFIGURATION_BUILD_DIR` on a subsequent `xcodebuild` command: CMake's generated
link commands already refer to configuration-specific library paths.

Optional provisioning and installation use [the device procedure](device-check.md). A transport
IPA is not an Apple-provisioned distribution and does not demonstrate a device run.
