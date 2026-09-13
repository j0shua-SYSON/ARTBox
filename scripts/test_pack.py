"""Build the NDK ELF fixture and run the build-time packaging contract."""

import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from environment import ROOT, environment
from ndk import ARCHIVES, NAME, REVISION, obtain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    ndk = obtain(args.ndk_root)
    host = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    suffix = ".exe" if os.name == "nt" else ""
    clang = ndk / "toolchains/llvm/prebuilt" / host / "bin" / f"clang{suffix}"
    build = args.build_dir.resolve() if args.build_dir else Path(os.environ["ARTBOX_BUILD_DIR"]) / "m1"
    build.mkdir(parents=True, exist_ok=True)
    fixture = build / "hello.elf"
    subprocess.run([
        str(clang), "--target=aarch64-linux-android28", "-nostdlib", "-static", "-Wl,-no-pie",
        "-Wl,--build-id=none", "-Wl,--emit-relocs", "-Wl,-z,max-page-size=16384",
        f"-Wl,-T,{ROOT / 'fixtures/hello/hello.ld'}", str(ROOT / "fixtures/hello/hello.S"),
        "-o", str(fixture),
    ], check=True)
    archive_host, archive_size, archive_sha1 = ARCHIVES[sys.platform]
    archive_name = f"{NAME}-{archive_host}.zip"
    sha_file = Path(os.environ["ARTBOX_CACHE_DIR"]) / "downloads" / f"{archive_name}.sha256"
    configured = bool(args.ndk_root or os.environ.get("ARTBOX_NDK_ROOT"))
    provenance = {
        "ndk_revision": REVISION,
        "ndk_source": "configured installation" if configured else "managed cache",
        "clang_version": subprocess.check_output([str(clang), "--version"], text=True).splitlines()[0],
        "target": "aarch64-linux-android28", "libc": "none (-nostdlib)",
        "source": "fixtures/hello/hello.S", "linker_script": "fixtures/hello/hello.ld",
        "archive": archive_name, "archive_bytes": archive_size, "archive_sha1": archive_sha1,
        "archive_sha256": sha_file.read_text().strip() if not configured and sha_file.is_file() else None,
        "elf_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
    }
    (build / "elf-build.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    subprocess.run([sys.executable, "-B", str(ROOT / "tests/check_pack.py"),
                    str(ROOT / "tools/pack_elf.py"), str(fixture), str(build / "pack-tests")], check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ARTBox packaging test: {error}", file=sys.stderr)
        sys.exit(1)
