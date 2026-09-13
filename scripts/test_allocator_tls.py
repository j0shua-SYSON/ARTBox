"""Compare the pinned allocator TLS caller on native Linux ARM64."""
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
    parser.add_argument("--compiler", default=os.environ.get("CXX", "clang++"))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != "linux" or platform.machine().lower() not in ("aarch64", "arm64"):
        parser.error("This oracle requires native Linux ARM64")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/allocator-tls-oracle"
    build.mkdir(parents=True, exist_ok=True)
    records = {}
    for profile in ("upstream", "native"):
        report = json.loads((args.evidence_root / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        metadata = report["allocator_tls"]
        obj = args.evidence_root / f"build/m2/bionic/{profile}/allocator-tls-test.o"
        if hashlib.sha256(obj.read_bytes()).hexdigest() != metadata["object_sha256"]:
            raise RuntimeError("Allocator TLS caller differs from its NDK build")
        reads = metadata["inventory"]["tpidr_el0_read"]
        if (profile == "upstream" and reads == 0) or (profile == "native" and reads != 0):
            raise RuntimeError("Allocator TLS comparison lost its original or adapted path")
        executable = build / profile
        subprocess.run([args.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18", "-pthread",
                        str(ROOT / "fixtures/bionic-allocator-tls/check.cpp"), str(obj.resolve()), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        (build / f"{profile}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        record = json.loads(result.stdout)
        if record != {"threads": 8, "exchanges": 8192, "failures": 0}:
            raise RuntimeError("Allocator TLS oracle did not complete every exchange")
        records[profile] = {**record, **metadata, "source_commit": report["dependencies"]["gwp_asan"]["commit"]}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-allocator-tls-linux.json").write_text(json.dumps({
        "scope": "Pinned GWP-ASan TLS state and platform hook; no allocator or complete Bionic startup",
        "profiles": records}, indent=2) + "\n", encoding="utf-8")
    print("Original and adapted GWP-ASan TLS each pass 8192 exchanges on eight native threads")


if __name__ == "__main__":
    main()
