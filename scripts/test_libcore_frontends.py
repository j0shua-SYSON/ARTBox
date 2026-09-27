"""Compare numeric resolver/string callers with native Linux libc."""
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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--compiler", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != "linux" or platform.machine().lower() not in ("aarch64", "arm64"):
        parser.error("This reference requires native Linux ARM64")
    producer = args.evidence_root / "artifacts/m2-bionic-startup.json"
    report = json.loads(producer.read_text(encoding="utf-8"))
    metadata = report["libcore_frontends"]
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if report["project_commit"] != head or metadata["common_cases"] != 45 or metadata["account_cases"] != 30 or \
            any(report[mode]["libcore_frontend_cases"] != 75 for mode in ("native", "sampled_native")):
        raise RuntimeError("Both signed Bionic modes must pass this revision's frontend contract")
    for name in ("common", "accounts"):
        if digest(ROOT / f"fixtures/bionic-libcore/{name}.c") != metadata[name + "_source_sha256"] or \
                digest(args.evidence_root / f"build/m2/bionic-startup/libcore-{name}.o") != metadata[name + "_object_sha256"]:
            raise RuntimeError("Class-library frontend caller differs from the signed producer")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m3/libcore-frontends"
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "check"
    command = [args.compiler, "-std=c11", "-O2", "-fno-builtin", "-Wall", "-Wextra", "-Werror",
               str(ROOT / "fixtures/bionic-libcore/common.c"), str(ROOT / "fixtures/bionic-libcore/linux.c"),
               "-o", str(binary)]
    result = subprocess.run(command, capture_output=True)
    (build / "build.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        sys.stderr.buffer.write(result.stdout + result.stderr)
        result.check_returncode()
    result = subprocess.run([str(binary)], capture_output=True, timeout=30)
    (build / "reference.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        sys.stderr.buffer.write(result.stdout + result.stderr)
        result.check_returncode()
    observed = json.loads(result.stdout)
    if observed != {"cases": 45, "result": 45}:
        raise RuntimeError("The common libc frontends did not complete their reference contract")
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    record = {"scope": "Numeric addresses and bounded strings; no DNS/network service, Android account checks execute only in signed Bionic",
              "project_commit": head, "producer_sha256": digest(producer), "metadata": metadata,
              "native_linux": observed, "reference_binary_sha256": digest(binary), "command": command}
    (artifacts / "m3-libcore-frontends-linux.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print("45 numeric resolver/string cases pass on Linux and signed Bionic; Android's 30 account cases pass in both signed modes")


if __name__ == "__main__":
    main()
