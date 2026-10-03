"""Execute the exact adapted Bionic vfork bytes with injected replies on ARM64."""
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
    parser.add_argument("--compiler", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != "linux" or platform.machine().lower() not in ("aarch64", "arm64"):
        parser.error("This oracle requires native Linux ARM64")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m3/vfork-oracle"
    build.mkdir(parents=True, exist_ok=True)
    report = json.loads((args.evidence_root / "artifacts/m2-bionic-native.json").read_text(encoding="utf-8"))
    metadata = report["vfork"]
    startup = json.loads((args.evidence_root / "artifacts/m2-bionic-startup.json").read_text(encoding="utf-8"))
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if startup["project_commit"] != head or startup["vfork"]["capture"] != metadata or \
            any(startup[mode]["vfork_cases"] != 30 for mode in ("native", "sampled_native")):
        raise RuntimeError("Both signed Bionic modes must pass this revision's vfork contract")
    if metadata["cases"] != 28 or metadata["mutation_result"] != -1000:
        raise RuntimeError("The vfork oracle contract changed")
    for name, expected in metadata["fixture_sources"].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Vfork oracle source changed: {name}")
    records = {}
    for mode in ("native", "mutant"):
        obj = args.evidence_root / "build/m2/bionic/native/vfork" / (mode + "-test.o")
        if hashlib.sha256(obj.read_bytes()).hexdigest() != metadata[mode]["object_sha256"]:
            raise RuntimeError("Vfork oracle differs from its producer")
        executable = build / mode
        subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                        str(ROOT / "fixtures/bionic-vfork/linux.c"), str(obj.resolve()),
                        "-o", str(executable)], check=True)
        result = subprocess.run([str(executable), *(["--expect-failure"] if mode == "mutant" else [])],
                                capture_output=True, text=True, timeout=30)
        (build / (mode + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            print(result.stdout + result.stderr, end="")
            result.check_returncode()
        record = json.loads(result.stdout)
        if record["result"] != (28 if mode == "native" else -1000) or \
                record["mutation"] != (mode == "mutant") or record["elapsed_ns"] <= 0:
            raise RuntimeError("The native vfork caller did not complete its contract")
        records[mode] = record
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m3-vfork-linux.json").write_text(json.dumps({
        "scope": "Original Bionic flags, cached state and errno with translated boundaries; injected replies, no processes created",
        "project_commit": head, "metadata": metadata, "execution": records}, indent=2) + "\n", encoding="utf-8")
    print("Bionic vfork passes 28 captured-reply cases; missing x9/x10 preservation fails as expected")


if __name__ == "__main__":
    main()
