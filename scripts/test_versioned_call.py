"""Run the signed Bionic fixture's exact versioned NDK inputs on native Linux ARM64."""
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
    if sys.platform != "linux" or platform.machine().lower() not in ("arm64", "aarch64"):
        parser.error("Native Linux ARM64 is required")
    evidence = args.evidence_root.resolve()
    report = json.loads((evidence / "artifacts/m2-bionic-startup.json").read_text(encoding="utf-8"))
    metadata = report["versions"]
    inputs = evidence / "build/m2/bionic-startup"
    provider, client = inputs / "libartbox_versions.so", inputs / "version-client.o"
    expected = report["images"]["versions"]["elf_sha256"]
    if digest(provider) != expected or digest(client) != metadata["client_object_sha256"] or \
            digest(ROOT / "fixtures/dynamic/version_client.c") != metadata["client_source_sha256"] or \
            digest(ROOT / "fixtures/dynamic/versions.c") != metadata["provider_source_sha256"] or \
            digest(ROOT / "fixtures/dynamic/versions.map") != metadata["version_map_sha256"]:
        raise RuntimeError("Versioned inputs differ from the signed Bionic run")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/versioned-call"
    build.mkdir(parents=True, exist_ok=True)
    executable = build / "versioned-call"
    subprocess.run([args.compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-ffixed-x18",
                    str(ROOT / "fixtures/dynamic/linux_versions.c"), str(client), str(provider),
                    "-Wl,-rpath," + str(inputs), "-o", str(executable)], check=True)
    process = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf-8", timeout=15)
    (build / "native.log").write_text(process.stdout + process.stderr, encoding="utf-8")
    if process.returncode:
        raise RuntimeError(f"Native version calls failed ({process.returncode}): {process.stdout}{process.stderr}")
    result = json.loads(process.stdout)
    if result != {"version_result": 46} or any(report[key]["version_result"] != 46 for key in ("native", "sampled_native")):
        raise RuntimeError("Version calls disagree")
    (Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "m2-versioned-linux.json").write_text(json.dumps({
        "scope": "Exact NDK versioned provider ELF and caller object, signed Bionic compared with native Linux",
        "elf_sha256": expected, "versions": metadata, **result}, indent=2) + "\n", encoding="utf-8")
    print("Native Linux and both signed Bionic processes agree on hidden/default version calls")


if __name__ == "__main__":
    main()
