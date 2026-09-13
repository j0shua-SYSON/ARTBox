"""Execute NDK-built Bionic syscall entries on a native Linux ARM64 oracle.

The capture backend tests every entry without submitting arbitrary syscalls.
Five selected smoke cases compare the adapted and original Linux paths.
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
    parser.add_argument("--compiler", default=os.environ.get("CXX", "clang++"))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != "linux" or platform.machine().lower() not in ("aarch64", "arm64"):
        parser.error("Executing these Android ELF objects requires native Linux ARM64")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/syscall-oracle"
    build.mkdir(parents=True, exist_ok=True)
    harness = build / "invoke.o"
    subprocess.run([args.compiler, "-c", str(ROOT / "fixtures/bionic-syscalls/invoke.S"), "-o", str(harness)], check=True)
    records = {}
    for profile in ("upstream", "native"):
        report = json.loads((args.evidence_root / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        metadata = report["syscall_stubs"]
        directory = args.evidence_root / f"build/m2/bionic/{profile}"
        obj, header = directory / "syscall-test.o", directory / "generated/syscall-test.h"
        for path, key in ((obj, "test_object_sha256"), (header, "test_header_sha256")):
            if hashlib.sha256(path.read_bytes()).hexdigest() != metadata[key]:
                raise RuntimeError(f"Bionic syscall test input differs from its NDK build: {path.name}")
        executable = build / profile
        command = [args.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18", "-pthread",
                   "-I", str(header.parent.resolve()), str(ROOT / "fixtures/bionic-syscalls/check.cpp"),
                   str(obj.resolve()), str(harness), "-o", str(executable)]
        if profile == "native":
            command.append("-DARTBOX_ADAPTED_STUBS=1")
        subprocess.run(command, check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        (build / f"{profile}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        record = json.loads(result.stdout)
        functions = metadata["stubs"] + metadata["aliases"] + 1
        if record["functions"] != functions or record["capture_cases"] != (functions * 9 * 4 if profile == "native" else 0) or \
                record["smoke_cases"] != 5 or record["failures"]:
            raise RuntimeError("Bionic syscall ABI/errno checks did not meet the complete function contract")
        records[profile] = {**record, "object_sha256": metadata["test_object_sha256"],
                            "source_commit": report["source_commit"]}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-syscall-stubs-linux.json").write_text(json.dumps({
        "scope": "NDK-built syscall entries and Bionic errno helper on native Linux; no complete libc or Darwin translation",
        "compiler": subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0],
        "profiles": records}, indent=2) + "\n", encoding="utf-8")
    print(f"{functions} entries passed argument/return/errno/register checks; original and adapted paths pass five Linux smoke cases")


if __name__ == "__main__":
    main()
