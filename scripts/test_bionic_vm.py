"""Run the signed-wrapper NDK memory caller with Bionic on native Linux ARM64."""
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
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/vm-oracle"
    build.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((args.evidence_root / "artifacts/m2-dynamic-wrapper.json").read_text(encoding="utf-8"))
    metadata = manifest["vm"]
    obj = args.evidence_root / "build/m2/dynamic-wrapper/vm-check.o"
    source = ROOT / "fixtures/bionic-vm/check.c"
    if metadata["cases"] != 35 or hashlib.sha256(obj.read_bytes()).hexdigest() != metadata["object_sha256"] or \
            hashlib.sha256(source.read_bytes()).hexdigest() != metadata["source_sha256"]:
        raise RuntimeError("VM oracle differs from the NDK caller tested in the signed framework")
    records = {}
    for profile in ("upstream", "native"):
        report = json.loads((args.evidence_root / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        stubs = args.evidence_root / f"build/m2/bionic/{profile}/syscall-test.o"
        if hashlib.sha256(stubs.read_bytes()).hexdigest() != report["syscall_stubs"]["test_object_sha256"]:
            raise RuntimeError("VM oracle syscall entries differ from the checked Bionic build")
        executable = build / profile
        subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                        str(ROOT / "fixtures/bionic-vm/linux.c"), str(obj.resolve()), str(stubs.resolve()),
                        "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        (build / f"{profile}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        record = json.loads(result.stdout)
        if record["cases"] != 35 or record["page_size"] < 4096 or record["elapsed_ns"] <= 0:
            raise RuntimeError("VM oracle did not complete its memory/errno contract")
        records[profile] = {**record, "syscall_object_sha256": report["syscall_stubs"]["test_object_sha256"]}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-vm-linux.json").write_text(json.dumps({
        "scope": "Identical NDK memory caller through original/adapted Bionic stubs on Linux; no complete libc startup",
        "vm": metadata, "profiles": records}, indent=2) + "\n", encoding="utf-8")
    print("Both Bionic paths pass 35 memory and errno cases with the signed-wrapper NDK caller")


if __name__ == "__main__":
    main()
