"""Build the NDK ELF fixture and run the build-time packaging contract."""

import sys

sys.dont_write_bytecode = True

import argparse
import os
from pathlib import Path
import subprocess

from environment import ROOT, environment
from ndk import obtain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    ndk = obtain(args.ndk_root)
    host = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    suffix = ".exe" if os.name == "nt" else ""
    clang = ndk / "toolchains/llvm/prebuilt" / host / "bin" / f"clang{suffix}"
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "m1"
    build.mkdir(parents=True, exist_ok=True)
    fixture = build / "hello.elf"
    subprocess.run([
        str(clang), "--target=aarch64-linux-android28", "-nostdlib", "-static", "-Wl,-no-pie",
        "-Wl,--build-id=none", "-Wl,--emit-relocs", "-Wl,-z,max-page-size=16384",
        f"-Wl,-T,{ROOT / 'fixtures/hello/hello.ld'}", str(ROOT / "fixtures/hello/hello.S"),
        "-o", str(fixture),
    ], check=True)
    subprocess.run([sys.executable, "-B", str(ROOT / "tests/check_pack.py"),
                    str(ROOT / "tools/pack_elf.py"), str(fixture), str(build / "pack-tests")], check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ARTBox packaging test: {error}", file=sys.stderr)
        sys.exit(1)
