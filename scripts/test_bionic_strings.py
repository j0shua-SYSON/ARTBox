"""Run the exact NDK string objects and scalar oracle on native Linux ARM64."""
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
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/strings-oracle"
    build.mkdir(parents=True, exist_ok=True)
    records = {}
    for profile in ("upstream", "native"):
        report = json.loads((args.evidence_root / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        obj = args.evidence_root / f"build/m2/bionic/{profile}/strings-test.o"
        metadata = report["strings"]
        if hashlib.sha256(obj.read_bytes()).hexdigest() != metadata["object_sha256"] or metadata["cases"] != 35908:
            raise RuntimeError("String oracle differs from its NDK build or case inventory")
        executable = build / profile
        subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                        str(ROOT / "fixtures/bionic-strings/linux.c"), str(obj.resolve()), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
        (build / f"{profile}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        record = json.loads(result.stdout)
        if record["cases"] != 35908 or record["page_size"] < 4096 or record["elapsed_ns"] <= 0:
            raise RuntimeError("String oracle did not complete every guarded-page case")
        records[profile] = {**record, **metadata}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-strings-linux.json").write_text(json.dumps({
        "scope": "Pinned baseline AOSP ARM64 string routines with independent scalar and guarded-page checks",
        "profiles": records}, indent=2) + "\n", encoding="utf-8")
    print("Both NDK string objects pass 35908 guarded-page cases on native Linux ARM64")


if __name__ == "__main__":
    main()
