"""Check the exact Android arithmetic object used in the signed Apple wrapper."""
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
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/binary128-oracle"
    build.mkdir(parents=True, exist_ok=True)
    report = json.loads((args.evidence_root / "artifacts/m2-dynamic-wrapper.json").read_text(encoding="utf-8"))
    metadata = report["binary128"]
    obj = args.evidence_root / "build/m2/bionic/native/binary128-test.o"
    source = ROOT / "fixtures/bionic-binary128/check.c"
    pin = json.loads((ROOT / "third_party/bionic/builtins.json").read_text(encoding="utf-8"))
    if metadata["cases"] != 123 or metadata["pin"] != pin or \
            hashlib.sha256(obj.read_bytes()).hexdigest() != metadata["object_sha256"] or \
            hashlib.sha256(source.read_bytes()).hexdigest() != metadata["source_sha256"]:
        raise RuntimeError("Arithmetic oracle differs from the reviewed NDK input")
    executable = build / "check"
    subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                    str(ROOT / "fixtures/bionic-binary128/linux.c"), str(obj.resolve()), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
    (build / "check.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    result.check_returncode()
    record = json.loads(result.stdout)
    if record["cases"] != 123 or record["elapsed_ns"] <= 0:
        raise RuntimeError("Android binary128 arithmetic did not complete its contract")
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-binary128-linux.json").write_text(json.dumps({
        "scope": "Android compiler-rt binary128 helpers; integer-only test boundary, no host long-double adapter",
        **metadata, **record}, indent=2) + "\n", encoding="utf-8")
    print("Android binary128 helpers pass 123 exact-bit arithmetic and comparison cases")


if __name__ == "__main__":
    main()
