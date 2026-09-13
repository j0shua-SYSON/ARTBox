"""Compare Bionic's original/adapted inline signal paths using native Linux.

This uses the system libc and a Linux test endpoint. It tests the source
adaptation and signal arguments, not Bionic loading or Darwin signal support.
"""
import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

from environment import ROOT, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != "linux" or platform.machine().lower() not in ("aarch64", "arm64", "x86_64"):
        parser.error("This syscall comparison requires native 64-bit Linux")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/inline-raise"
    build.mkdir(parents=True, exist_ok=True)
    records = {}
    for profile in ("upstream", "native"):
        manifest = json.loads((args.evidence_root / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        header = args.evidence_root / f"build/m2/bionic/{profile}/bionic_inline_raise.h"
        digest = hashlib.sha256(header.read_bytes()).hexdigest()
        if digest != manifest["inline_raise_sha256"]:
            raise RuntimeError("Exported Bionic inline-raise source differs from its build evidence")
        executable = build / profile
        command = [args.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
                   "-I", str(header.parent.resolve()), str(ROOT / "fixtures/bionic-inline-raise/check.cpp"), "-o", str(executable)]
        if profile == "native":
            command.append("-DARTBOX_NATIVE_HOST=1")
        subprocess.run(command, check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        (build / f"{profile}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        record = json.loads(result.stdout)
        if record["failures"] or record["threads"] != 8 or record["deliveries"] != 1024 or \
                record["endpoint_calls"] != (1032 if profile == "native" else 0):
            raise RuntimeError("Inline signal comparison did not meet its delivery/errno contract")
        records[profile] = {**record, "header_sha256": digest, "source_commit": manifest["source_commit"]}
    report = {"scope": "Native Linux source-adaptation test using system libc; no Bionic loader or Darwin signals",
              "architecture": platform.machine(), "profiles": records}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-inline-raise-linux.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Both inline signal paths passed 1024 thread-directed deliveries and preserved errno on invalid signals")


if __name__ == "__main__":
    main()
