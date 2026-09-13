"""Compare portable ELF data validation with LLVM on real NDK shared objects."""
import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from environment import ROOT, environment
from ndk import REVISION, obtain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--host-dir", type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    builds = Path(os.environ["ARTBOX_BUILD_DIR"])
    build = args.build_dir.resolve() if args.build_dir else builds / "m2/metadata"
    host = args.host_dir.resolve() if args.host_dir else builds / "host"
    suffix = ".exe" if os.name == "nt" else ""
    inspector = next((p for p in (host / f"artbox_inspect_elf{suffix}",
                                 host / "Release" / f"artbox_inspect_elf{suffix}") if p.is_file()), None)
    if inspector is None:
        raise RuntimeError("Build the portable host before running dynamic ELF checks")
    ndk = obtain(args.ndk_root)
    platform = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    tools = ndk / "toolchains/llvm/prebuilt" / platform / "bin"
    build.mkdir(parents=True, exist_ok=True)
    clang = tools / f"clang{suffix}"
    flags = [str(clang), "--target=aarch64-linux-android35", "-std=c11", "-fPIC", "-fno-emulated-tls",
             "-shared", "-nostdlib", "-Wl,-z,max-page-size=16384", "-Wl,--hash-style=both",
             "-Wl,--pack-dyn-relocs=relr"]
    results = []
    rejected = 0
    for name, source in (("dependency", "dependency.c"), ("metadata", "library.c")):
        binary = build / f"libartbox_{name}.so"
        command = flags + [str(ROOT / "fixtures/dynamic" / source), f"-Wl,-soname,{binary.name}", "-o", str(binary)]
        if name == "metadata":
            command += ["-L", str(build), "-lartbox_dependency"]
        subprocess.run(command, check=True)
        original = binary.read_bytes()
        reference = json.loads(subprocess.check_output([
            str(tools / f"llvm-readobj{suffix}"), "--elf-output-style=JSON", "--file-headers",
            "--program-headers", str(binary)]))[0]
        programs = [p["ProgramHeader"] for p in reference["ProgramHeaders"]]
        header = reference["ElfHeader"]
        if not header["Type"].endswith("(0x3)") or header["Machine"]["Value"] != 183:
            raise RuntimeError("Compiler did not produce an AArch64 shared object")
        expected = {"type": 3, "entry": header["Entry"], "phnum": header["ProgramHeaderCount"],
                    "dynamic": int(any(p["Type"]["Value"] == 2 for p in programs)),
                    "tls": int(any(p["Type"]["Value"] == 7 for p in programs)),
                    "relro": int(any(p["Type"]["Value"] == 0x6474e552 for p in programs)),
                    "loads": [{"offset": p["Offset"], "vaddr": p["VirtualAddress"], "filesz": p["FileSize"],
                               "memsz": p["MemSize"], "align": p["Alignment"], "flags": p["Flags"]["Value"]}
                              for p in programs if p["Type"]["Value"] == 1]}
        for stripped in (False, True):
            candidate = binary
            if stripped:
                contents = bytearray(original)
                struct.pack_into("<Q", contents, 40, 0)
                struct.pack_into("<3H", contents, 58, 0, 0, 0)
                candidate = build / f"{name}-without-sections.elf"
                candidate.write_bytes(contents)
            observed = json.loads(subprocess.check_output([str(inspector), str(candidate)]))
            if observed != expected:
                raise RuntimeError(f"Portable ELF view differs from LLVM for {name}, stripped={stripped}")
        phoff = header["ProgramHeaderOffset"]
        load = next(i for i, p in enumerate(programs) if p["Type"]["Value"] == 1)
        dynamic = next(i for i, p in enumerate(programs) if p["Type"]["Value"] == 2)
        mutations = [("program-table-overflow", 32, "<Q", (1 << 64) - 1),
                     ("writable-code", phoff + load * 56 + 4, "<I", 7),
                     ("memory-overflow", phoff + load * 56 + 40, "<Q", (1 << 64) - 1),
                     ("dynamic-file-alias", phoff + dynamic * 56 + 8, "<Q", programs[dynamic]["Offset"] + 8),
                     ("dynamic-entry-size", phoff + dynamic * 56 + 32, "<Q", programs[dynamic]["FileSize"] - 1)]
        for label, offset, fmt, value in mutations:
            contents = bytearray(original)
            struct.pack_into(fmt, contents, offset, value)
            candidate = build / f"{name}-{label}.elf"
            candidate.write_bytes(contents)
            check = subprocess.run([str(inspector), str(candidate)], capture_output=True)
            if check.returncode != 1 or check.stdout:
                raise RuntimeError(f"Malformed ELF was not rejected cleanly: {name}/{label}")
            rejected += 1
        if binary.read_bytes() != original:
            raise RuntimeError("Metadata inspection modified the original ELF")
        results.append({"file": binary.name, "sha256": hashlib.sha256(original).hexdigest(), "view": expected})
    report = {"scope": "ELF data validation only; no guest execution", "ndk_revision": REVISION,
              "compiler": subprocess.check_output([str(clang), "--version"], text=True).splitlines()[0],
              "valid_views": len(results) * 2, "rejected_mutations": rejected, "inputs": results}
    (Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "m2-metadata.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Dynamic ELF: {len(results) * 2} LLVM-matched views, {rejected} malformed inputs rejected; no guest execution")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ARTBox dynamic ELF: {error}", file=sys.stderr)
        sys.exit(1)
