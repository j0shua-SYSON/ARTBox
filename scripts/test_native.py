"""Execute both signed M1 inputs natively on an ARM64 Mac and record measurements."""

import sys

sys.dont_write_bytecode = True

import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

from environment import environment
from guest_bundle import prepare


def main():
    if sys.platform != "darwin" or platform.machine().lower() not in ("arm64", "aarch64"):
        raise RuntimeError("Native guest acceptance requires an ARM64 Mac")
    os.environ.update(environment())
    builds = Path(os.environ["ARTBOX_BUILD_DIR"])
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    fixture = builds / "m1/hello.elf"
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
