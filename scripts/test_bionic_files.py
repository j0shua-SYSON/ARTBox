"""Compare the identical NDK file caller through both Bionic Linux profiles."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
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
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/files-oracle"
    build.mkdir(parents=True, exist_ok=True)
    evidence = args.evidence_root.resolve()
    report = json.loads((evidence / "artifacts/m2-bionic-startup.json").read_text(encoding="utf-8"))
    metadata = report["files"]
    obj = evidence / "build/m2/bionic-startup/file-check.o"
    if metadata["cases"] != 41 or digest(obj) != metadata["object_sha256"] or \
            digest(ROOT / "fixtures/bionic-files/check.c") != metadata["source_sha256"]:
        raise RuntimeError("File caller differs from the signed Bionic startup input")
    mapping = evidence / "build/m2/bionic-startup/mapping-check.o"
    if report["mappings"]["cases"] != 43 or digest(mapping) != report["mappings"]["object_sha256"] or \
            digest(ROOT / "fixtures/bionic-files/mapping.c") != report["mappings"]["source_sha256"]:
        raise RuntimeError("Mapping caller differs from the signed Bionic startup input")
    unlink = evidence / "build/m2/bionic-startup/unlink-check.o"
    if report['unlink']['cases'] != 29 or digest(unlink) != report['unlink']['object_sha256'] or \
            digest(ROOT / 'fixtures/bionic-files/unlink.c') != report['unlink']['source_sha256']:
        raise RuntimeError('Unlink caller differs from the signed Bionic input')
    for mode in ('native', 'sampled_native'):
        if report[mode]['unlink_cases'] != 29:
            raise RuntimeError('Signed Bionic unlink checks did not complete')
    cwd = evidence / 'build/m2/bionic-startup/cwd-check.o'
    if (report['cwd']['cases'] != 22 or digest(cwd) != report['cwd']['object_sha256'] or
            digest(ROOT / 'fixtures/bionic-files/cwd.c') != report['cwd']['source_sha256'] or
            any(report[mode]['cwd_cases'] != 22 for mode in ('native', 'sampled_native'))):
        raise RuntimeError('Getcwd caller differs from the signed Bionic input')
    records = {}
    for profile in ("upstream", "native"):
        control = json.loads((evidence / f"artifacts/m2-bionic-{profile}.json").read_text(encoding="utf-8"))
        stubs = evidence / f"build/m2/bionic/{profile}/syscall-test.o"
        if digest(stubs) != control["syscall_stubs"]["test_object_sha256"]:
            raise RuntimeError("Bionic syscall input changed")
        executable = build / profile
        subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                        str(ROOT / "fixtures/bionic-files/linux.c"), str(obj), str(mapping), str(unlink), str(cwd), str(stubs), "-o", str(executable)], check=True)
        root = Path(tempfile.mkdtemp(prefix=profile + "-root-", dir=build))
        (root / "data").mkdir()
        process = subprocess.run([str(executable)], cwd=root, capture_output=True, text=True, encoding="utf-8", timeout=15)
        (build / (profile + ".log")).write_text(process.stdout + process.stderr, encoding="utf-8")
        if process.returncode:
            raise RuntimeError(f"{profile} file oracle failed ({process.returncode}):\n{process.stdout}{process.stderr}")
        record = json.loads(process.stdout)
        if record["cases"] != 41 or record["mapping_cases"] != 43 or record['unlink_cases'] != 29 or record['cwd_cases'] != 22:
            raise RuntimeError("File caller did not complete")
        records[profile] = {**record, "syscall_object_sha256": digest(stubs)}
    (Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "m2-files-linux.json").write_text(json.dumps({
        "scope": "Identical NDK file caller with actual Bionic syscall entries and errno helper",
        "files": metadata, "mappings": report["mappings"], "unlink": report['unlink'], "cwd": report['cwd'], "profiles": records}, indent=2) + "\n", encoding="utf-8")
    print("Both Bionic Linux profiles passed all 41 file, 43 mapping, 29 unlink and 22 getcwd cases")


if __name__ == "__main__":
    main()
