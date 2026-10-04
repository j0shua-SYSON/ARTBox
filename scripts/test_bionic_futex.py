"""Compare the identical NDK futex caller through both Bionic Linux profiles."""
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
    parser.add_argument("--compiler", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != "linux" or platform.machine().lower() not in ("aarch64", "arm64"):
        parser.error("This oracle requires native Linux ARM64")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/futex-oracle"
    build.mkdir(parents=True, exist_ok=True)
    evidence = args.evidence_root.resolve()
    report = json.loads((evidence / "artifacts/m2-bionic-startup.json").read_text(encoding="utf-8"))
    metadata = report["futex"]
    obj = evidence / "build/m2/bionic-startup/futex-check.o"
    if metadata["cases"] != 19 or digest(obj) != metadata["object_sha256"] or \
            digest(ROOT / "fixtures/bionic-futex/check.c") != metadata["source_sha256"]:
        raise RuntimeError("Futex caller differs from the signed Bionic startup input")
    records = {}
    requeue = evidence / 'build/m2/bionic-startup/futex-requeue.o'
    if (report['futex_requeue']['cases'] != 18 or digest(requeue) != report['futex_requeue']['object_sha256'] or
            digest(ROOT / 'fixtures/bionic-futex/requeue.c') != report['futex_requeue']['source_sha256'] or
            any(report[mode]['futex_requeue_cases'] != 18 for mode in ('native', 'sampled_native'))):
        raise RuntimeError('Private requeue caller differs from the signed Bionic input')
    for profile in ("upstream", "native"):
        control = json.loads((evidence / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        stubs = evidence / f"build/m2/bionic/{profile}/syscall-test.o"
        if digest(stubs) != control["syscall_stubs"]["test_object_sha256"]:
            raise RuntimeError("Bionic syscall input changed")
        executable = build / profile
        subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                        str(ROOT / "fixtures/bionic-futex/linux.c"), str(obj), str(requeue), str(stubs), "-o", str(executable)], check=True)
        process = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
        (build / (profile + ".log")).write_text(process.stdout + process.stderr, encoding="utf-8")
        process.check_returncode()
        result = json.loads(process.stdout)
        if result["cases"] != 19 or result['requeue_cases'] != 18:
            raise RuntimeError("Futex caller did not complete")
        records[profile] = {**result, "syscall_object_sha256": digest(stubs)}
    (Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "m2-futex-linux.json").write_text(json.dumps({
        "scope": "Identical NDK futex caller with actual Bionic syscall entries and errno helper",
        "futex": metadata, "requeue": report['futex_requeue'], "profiles": records}, indent=2) + "\n", encoding="utf-8")
    print("Both Bionic Linux profiles passed 19 futex and 18 requeue wire cases")


if __name__ == "__main__":
    main()
