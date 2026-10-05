# ARTBox

An experimental Android userspace runtime for iOS 15 and later, with a portable
C/C++ core. Android ARM64 code is prepared ahead of time and packaged as signed
native code; ART's bytecode interpreter provides the initial DEX execution path.
No CPU emulation, guest kernel, JIT, private entitlements, or jailbreak dependency.

**Original AOSP ART executes hello DEX on an iPhone 6s Plus running iOS 15.8.5**,
with collection, exceptions, native-thread attachment and shutdown verified.
Bionic also passes all 328 M2 expectations on that phone. Background/foreground
and cold home-screen relaunch pass without a debugger. The test phone is already
jailbroken; all installed code images match the CI artifacts, but ordinary
stock-device provisioning remains unverified.

The same interpreter and managed checks pass on native Linux ARM64 and through
signed frameworks on macOS ARM64, with JIT disabled. General Android app
support, Activities and graphics remain future milestones.
See [status](docs/STATUS.md), [architecture decisions](docs/DECISIONS.md), and the
[ELF-to-Mach-O versus wrapper design](docs/loader-design.md).

## Build the portable core

Install Python 3.9+, CMake 3.24+, and C11/C++11 compilers using your preferred tooling.
The runtime layer is C; the host thread-isolation test uses C++ standard threads.
Use the same Python entry point on Windows, macOS, and Linux:

```sh
python scripts/build.py host
```

Use `python3` where that is the Python 3 command. CMake chooses the platform's
default generator; pass `--generator Ninja` to use Ninja. The command builds,
runs CTest, verifies `ARTBox ready`, and records host startup measurements.
It does not install tools or packages.

Build, cache, scratch, and artifact paths are configurable. Defaults are
`build/`, `.env/`, `.tmp/`, and `artifacts/` within the checkout. Existing SDK,
cache, Git, and signing settings remain usable. Commands run directly in Python.
See [build configuration](docs/building.md) for options and direct CMake usage.
Personal scripts and local notes belong in ignored `work/`.

## Build the iPhone app

On a Mac with Xcode and the iOS SDK:

```sh
python3 scripts/build.py ios --with-guest
```

This generates an Xcode project, builds the arm64 iOS 15 device target, checks
the signature and empty entitlements, and writes `artifacts/ARTBox.ipa` with a
provenance manifest. `--with-guest` builds the M1 fixture with pinned NDK r28c
and embeds both signed frameworks. Omit it for the startup console alone.
GitHub Actions also produces a temporary IPA artifact. Its host workflow builds
the integrated `ARTBox-M2-ipa` after the Bionic suite and Linux comparisons pass.
To build that app locally, pass `--m2-evidence PATH` pointing to the startup
artifact from the same revision. See [M2 acceptance](docs/acceptance/m2.md).

The IPA has an ad-hoc transport signature (`codesign -s -`). Installation needs
ordinary development or Ad Hoc provisioning. See [device checks](docs/device-check.md).
Physical-device checks are optional for all milestones. CI build verification
and actual execution evidence are recorded separately.

ARTBox's original code is [MIT licensed](LICENSE). No proprietary Android
components or APKs are bundled. Dependency licenses retain their own terms;
see [THIRD_PARTY.md](THIRD_PARTY.md).
