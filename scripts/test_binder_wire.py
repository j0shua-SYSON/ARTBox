"""Compare the portable Binder boundary with a pinned ARM64 NDK UAPI producer."""
import sys
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from environment import ROOT, environment
from ndk import obtain, REVISION


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-test", type=Path, required=True)
    parser.add_argument("--ndk", type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    ndk = obtain(args.ndk)
    host = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    tools = ndk / "toolchains/llvm/prebuilt" / host
    suffix = ".exe" if os.name == "nt" else ""
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m4/binder-wire"
    build.mkdir(parents=True, exist_ok=True)
    obj, fixture = build / "uapi.o", build / "uapi.bin"
    source = ROOT / "fixtures/binder-wire/uapi.c"
    command = [str(tools / "bin" / ("clang" + suffix)), "--target=aarch64-linux-android35",
               "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-ffreestanding",
               "-I", str(ROOT / "core/include"), "-c", str(source), "-o", str(obj)]
    subprocess.run(command, check=True)
    subprocess.run([str(tools / "bin" / ("llvm-objcopy" + suffix)),
                    "--dump-section", f".artbox.binder={fixture}", str(obj)], check=True)
    result = subprocess.run([str(args.host_test.resolve()), str(fixture)], capture_output=True, text=True, timeout=20)
    (build / "host-reader.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    result.check_returncode()
    if "NDK ARM64 UAPI fixture:" not in result.stdout or fixture.stat().st_size != 156:
        raise RuntimeError("The independent UAPI fixture was not verified")
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    records = {
        "scope": "Compile-time ARM64 NDK UAPI comparison plus native host byte decoding; no Binder driver execution",
        "project_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
        "ndk_revision": REVISION,
        "constant_comparisons": len(re.findall(r"^SAME\(", source.read_text(), re.MULTILINE)),
        "fixture_bytes": fixture.stat().st_size,
        "object_sha256": sha(obj), "fixture_sha256": sha(fixture),
        "host_reader_sha256": sha(args.host_test),
        "uapi_header_sha256": sha(tools / "sysroot/usr/include/linux/android/binder.h"),
        "project_files": {str(p.relative_to(ROOT)).replace("\\", "/"): sha(p) for p in (
            source, ROOT / "core/include/artbox/binder_wire.h", ROOT / "core/src/binder_wire.c",
            ROOT / "tests/test_binder_wire.c", Path(__file__).resolve())},
    }
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m4-binder-wire.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(result.stdout, end="")
    print(f"{records['constant_comparisons']} constants agree with NDK {REVISION}; artifact hashes recorded")


if __name__ == "__main__":
    main()
