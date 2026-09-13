"""Build and validate ARTBox on Windows, macOS or Linux using standard tools."""

import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import statistics
import subprocess
import tempfile
import time
import zipfile

from environment import ROOT, environment


def run(*args, capture=False, cwd=ROOT):
    command = [str(arg) for arg in args]
    print("+ " + subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(command, cwd=cwd, check=True, capture_output=capture)
    return result.stdout if capture else b""


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def host(args, build, artifacts):
    command = ["cmake", "-S", ROOT, "-B", build, f"-DCMAKE_BUILD_TYPE={args.config}"]
    if args.generator:
        command += ["-G", args.generator]
    run(*command, *args.cmake_arg)
    run("cmake", "--build", build, "--config", args.config, "--parallel", args.jobs)
    run("ctest", "--test-dir", build, "-C", args.config, "--output-on-failure")
    suffix = ".exe" if os.name == "nt" else ""
    executable = build / f"artbox_host{suffix}"
    if not executable.is_file():
        executable = build / args.config / f"artbox_host{suffix}"
    timings = []
    for _ in range(20):
        start = time.perf_counter_ns()
        result = subprocess.run([str(executable)], capture_output=True, check=True, timeout=10)
        timings.append((time.perf_counter_ns() - start) / 1_000_000)
        if result.stdout.replace(b"\r\n", b"\n") != b"ARTBox ready\n" or result.stderr:
            raise RuntimeError("Startup sample did not satisfy the stdout/exit contract")
    metrics = {
        "scope": "Host CLI launch through exit, including process/capture overhead; not iOS startup",
        "platform": platform.platform(), "architecture": platform.machine(),
        "samples": len(timings), "milliseconds": timings,
        "median_ms": statistics.median(timings), "minimum_ms": min(timings),
        "maximum_ms": max(timings), "executable_bytes": executable.stat().st_size,
    }
    save_json(artifacts / "host-metrics.json", metrics)
    print(f"Host startup: {len(timings)} verified samples, median {metrics['median_ms']:.3f} ms")


def ios(args, build, artifacts):
    if sys.platform != "darwin":
        raise RuntimeError("The iOS device build requires Xcode on macOS")
    target = args.deployment_target
    run("cmake", "-S", ROOT, "-B", build, "-G", "Xcode",
        "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphoneos",
        "-DCMAKE_OSX_ARCHITECTURES=arm64", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={target}",
        f"-DARTBOX_BUNDLE_ID={args.bundle_id}",
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO", *args.cmake_arg)
    run("xcodebuild", "-project", build / "ARTBox.xcodeproj", "-scheme", "ARTBox",
        "-configuration", args.config, "-sdk", "iphoneos", "-destination", "generic/platform=iOS",
        "-derivedDataPath", build / "derived-data", f"OBJROOT={build / 'intermediates'}",
        f"SYMROOT={build / 'products'}",
        f"CLANG_MODULE_CACHE_PATH={os.environ['CLANG_MODULE_CACHE_PATH']}",
        "CODE_SIGNING_ALLOWED=NO", "CODE_SIGNING_REQUIRED=NO", "CODE_SIGN_IDENTITY=", "build")
    # CMake embeds library output paths in the generated link commands.
    # CONFIGURATION_BUILD_DIR must not be overridden at build time.
    app = build / f"{args.config}-iphoneos/ARTBox.app"
    binary = app / "ARTBox"
    declared = plistlib.loads((ROOT / "app/ARTBox.entitlements").read_bytes())
    info = plistlib.loads((app / "Info.plist").read_bytes())
    if declared or info.get("CFBundleIdentifier") != args.bundle_id or \
            info.get("MinimumOSVersion") != target or info.get("CFBundleExecutable") != "ARTBox":
        raise RuntimeError("Unexpected app entitlements, identifier, deployment target or executable")
    run("codesign", "--force", "--sign", "-", "--timestamp=none",
        "--entitlements", ROOT / "app/ARTBox.entitlements", app)
    run("codesign", "--verify", "--strict", "--verbose=2", app)
    reports = {
        "embedded-entitlements.plist": ("codesign", "--display", "--entitlements", ":-", app),
        "macho-build.txt": ("xcrun", "vtool", "-show-build", binary),
        "macho-load-commands.txt": ("xcrun", "otool", "-l", binary),
        "macho-dependencies.txt": ("xcrun", "otool", "-L", binary),
        "macho-symbols.txt": ("xcrun", "nm", binary),
    }
    output = {name: run(*command, capture=True) for name, command in reports.items()}
    for name, data in output.items():
        (artifacts / name).write_bytes(data)
    if plistlib.loads(output["embedded-entitlements.plist"]) != {}:
        raise RuntimeError("The signed app contains unexpected entitlements")
    arch = run("xcrun", "lipo", "-archs", binary, capture=True).decode().strip()
    if arch != "arm64":
        raise RuntimeError(f"Expected an arm64 device binary, got {arch}")
    build_commands = output["macho-build.txt"].decode()
    if not re.search(r"platform\s+IOS\s", build_commands) or not re.search(
            rf"minos\s+{re.escape(target)}\s", build_commands):
        raise RuntimeError(f"Expected LC_BUILD_VERSION for an iOS {target} device")
    for segment in re.split(r"Load command \d+", output["macho-load-commands.txt"].decode()):
        if "LC_SEGMENT_64" not in segment:
            continue
        protection = re.search(r"initprot\s+(0x[0-9a-fA-F]+)", segment)
        if not protection:
            raise RuntimeError("Cannot read Mach-O segment permissions")
        if int(protection.group(1), 16) & 6 == 6:
            raise RuntimeError("Writable and executable Mach-O segment")
    if not re.search(r"\b_artbox_start$", output["macho-symbols.txt"].decode(), re.MULTILINE):
        raise RuntimeError("Portable startup entry was not linked into the app")
    ipa = artifacts / "ARTBox.ipa"
    with tempfile.TemporaryDirectory(prefix="ipa-", dir=os.environ["ARTBOX_TEMP_DIR"]) as scratch:
        staged_ipa = Path(scratch) / ipa.name
        with zipfile.ZipFile(staged_ipa, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(app.rglob("*")):
                if path.is_file():
                    archive.write(path, (Path("Payload") / app.name / path.relative_to(app)).as_posix())
        shutil.copyfile(staged_ipa, ipa)
    provenance = {
        "commit": run("git", "rev-parse", "HEAD", capture=True).decode().strip(),
        "working_tree_dirty": bool(run("git", "status", "--porcelain", capture=True).strip()),
        "xcode": run("xcodebuild", "-version", capture=True).decode().strip(),
        "sdk": run("xcrun", "--sdk", "iphoneos", "--show-sdk-version", capture=True).decode().strip(),
        "target": f"arm64-apple-ios{target}", "bundle_id": args.bundle_id,
        "signature": "ad-hoc transport; requires provisioning", "entitlements": {},
        "device_launch_verified": False, "ipa_sha256": hashlib.sha256(ipa.read_bytes()).hexdigest(),
        "ipa_bytes": ipa.stat().st_size, "executable_bytes": binary.stat().st_size,
    }
    save_json(artifacts / "build-info.json", provenance)
    print(json.dumps(provenance, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("host", "ios"))
    parser.add_argument("--build-dir", type=Path, help="Build directory for this target")
    parser.add_argument("--artifacts-dir", type=Path)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--temp-dir", type=Path)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--generator", help="Host CMake generator (otherwise CMake chooses)")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--deployment-target", default="15.0")
    parser.add_argument("--bundle-id", default="com.j0shua.ARTBox")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Repeat as --cmake-arg=-DNAME=VALUE")
    args = parser.parse_args()
    for key in ("cache", "temp", "artifacts"):
        value = getattr(args, f"{key}_dir")
        if value is not None:
            os.environ[f"ARTBOX_{key.upper()}_DIR"] = str(value.resolve())
    os.environ.update(environment())
    build = args.build_dir.resolve() if args.build_dir else Path(os.environ["ARTBOX_BUILD_DIR"]) / args.target
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    (host if args.target == "host" else ios)(args, build, artifacts)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ARTBox: {error}", file=sys.stderr)
        sys.exit(1)
