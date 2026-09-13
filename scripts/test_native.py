"""Validate native M1 execution on an ARM64 Mac or an ARM64 Linux oracle host."""

import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

from environment import environment
from guest_bundle import prepare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("macos", "linux"),
                        default="linux" if sys.platform == "linux" else "macos")
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--manifest", type=Path, help="Paired Mac manifest for the Linux oracle")
    args = parser.parse_args()
    if platform.machine().lower() not in ("arm64", "aarch64"):
        raise RuntimeError("Native guest acceptance requires an ARM64 CPU")
    os.environ.update(environment())
    builds = Path(os.environ["ARTBOX_BUILD_DIR"])
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    fixture = args.elf.resolve() if args.elf else builds / "m1/hello.elf"
    if args.platform == "linux":
        if sys.platform != "linux" or not args.manifest:
            raise RuntimeError("The Linux oracle requires Linux and the paired Mac manifest")
        manifest = json.loads(args.manifest.read_text())
        digest = hashlib.sha256(fixture.read_bytes()).hexdigest()
        if digest != manifest["pack"]["input_sha256"]:
            raise RuntimeError("Linux oracle input differs from the packaged Mac input")
        fixture.chmod(fixture.stat().st_mode | 0o111)
        output = subprocess.run([str(fixture)], capture_output=True, timeout=10)
        if output.returncode != 0 or output.stdout != b"hello from Android ARM64\n" or output.stderr:
            raise RuntimeError(f"Original Linux ELF failed with exit {output.returncode}")
        if hashlib.sha256(fixture.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Oracle execution changed the ELF bytes")
        (artifacts / "m1-linux.json").write_text(json.dumps({
            "scope": "Unchanged NDK ELF on native ARM64 Linux", "input_sha256": digest,
            "exit_status": output.returncode, "output": output.stdout.decode(),
            "platform": platform.platform(), "architecture": platform.machine(),
        }, indent=2) + "\n", encoding="utf-8")
        print("Linux ARM64: unchanged NDK ELF printed hello and exited 0", flush=True)
        return
    if sys.platform != "darwin":
        raise RuntimeError("Signed Mach-O acceptance requires macOS")
    runner = builds / "host/artbox_native_hello"
    if not fixture.is_file() or not runner.is_file():
        raise RuntimeError("Run scripts/build.py host and scripts/test_pack.py first")
    result = prepare(fixture, builds / "m1/macos", "macos")
    result["scope"] = "Signed native ARM64 code on macOS; no iOS device execution claim"
    result["rss_method"] = "Darwin getrusage RUSAGE_SELF ru_maxrss, bytes for the entire host process"
    for kind, data in result["frameworks"].items():
        output = subprocess.check_output([str(runner), data["binary"], str(result["pack"]["payload_bytes"])], timeout=30)
        measurements = json.loads(output)
        if measurements["output"] != "hello from Android ARM64\n" or measurements["exit_status"] != 0:
            raise RuntimeError("Unexpected native execution result")
        measurements["median_invocation_ns"] = statistics.median(measurements["invocation_ns"])
        data["native"] = measurements
        # Preserve a portable manifest; absolute build paths are local details.
        data["binary"] = Path(data["binary"]).name
        print(f"{kind}: native hello, exit 0, 100 invocations, median {measurements['median_invocation_ns'] / 1000:.3f} us", flush=True)
    (artifacts / "m1-native.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ARTBox native test: {error}", file=sys.stderr)
        sys.exit(1)
