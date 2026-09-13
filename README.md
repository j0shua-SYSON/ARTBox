# ARTBox

An experimental Android userspace runtime for iOS 15 and later, with a portable
C/C++ core. Android ARM64 code is prepared ahead of time and packaged as signed
native code; ART's bytecode interpreter is the planned fallback for DEX.
No CPU emulation, guest kernel, JIT, private entitlements, or jailbreak dependency.

**The startup console works. Android app execution is under development.**
See [status](docs/STATUS.md), [architecture decisions](docs/DECISIONS.md), and the
[ELF-to-Mach-O versus wrapper design](docs/loader-design.md).

## Build the portable core

Install Python 3.9+, CMake 3.24+, and a C11 compiler using your preferred tooling.
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
python3 scripts/build.py ios
```

This generates an Xcode project, builds the arm64 iOS 15 device target, checks
the signature and empty entitlements, and writes `artifacts/ARTBox.ipa` with a
provenance manifest. GitHub Actions also produces a temporary IPA artifact.

The IPA has an ad-hoc transport signature (`codesign -s -`). Installation needs
ordinary development or Ad Hoc provisioning. See [device checks](docs/device-check.md).
CI build verification and physical-device execution are recorded separately.

ARTBox's original code is [MIT licensed](LICENSE). No proprietary Android
components or APKs are bundled. Dependency licenses retain their own terms;
see [THIRD_PARTY.md](THIRD_PARTY.md).
