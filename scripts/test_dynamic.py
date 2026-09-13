"""Compare portable ELF data validation with LLVM on real NDK shared objects."""
import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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
    dynamic_rejected = 0
    for name, source in (("dependency", "dependency.c"), ("metadata", "library.c")):
        binary = build / f"libartbox_{name}.so"
        command = flags + [str(ROOT / "fixtures/dynamic" / source), f"-Wl,-soname,{binary.name}", "-o", str(binary)]
        if name == "metadata":
            command += ["-L", str(build), "-lartbox_dependency"]
        subprocess.run(command, check=True)
        original = binary.read_bytes()
        reference = json.loads(subprocess.check_output([
            str(tools / f"llvm-readobj{suffix}"), "--elf-output-style=JSON", "--file-headers",
            "--program-headers", "--dyn-symbols", str(binary)]))[0]
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
        # LLVM 19 emits its text printer for --dynamic-table even when JSON is
        # requested. Use readelf's tagged text for that table and JSON for symbols.
        dynamic_text = subprocess.check_output([str(tools / f"llvm-readelf{suffix}"), "-d", "--wide", str(binary)], text=True)
        def table_size(tag):
            match = re.search(r"\(" + tag + r"\)\s+(\d+)", dynamic_text)
            return int(match[1]) if match else 0
        soname = re.search(r"\(SONAME\)[^\n]*\[([^\]]*)\]", dynamic_text)
        symbols = [entry["Symbol"] for entry in reference["DynamicSymbols"]]
        expected_dynamic = {
            "needed": re.findall(r"\(NEEDED\)[^\n]*\[([^\]]*)\]", dynamic_text),
            "soname": soname[1] if soname else None,
            "rela_bytes": table_size("RELASZ"), "plt_rela_bytes": table_size("PLTRELSZ"),
            "relr_bytes": table_size("RELRSZ"), "init_array_bytes": table_size("INIT_ARRAYSZ"),
            "symbols": [{"name": s["Name"]["Name"], "value": s["Value"], "size": s["Size"],
                         "binding": s["Binding"]["Value"], "type": s["Type"]["Value"],
                         "visibility": s["Other"]["Value"] & 3, "section": s["Section"]["Value"]} for s in symbols]}
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
            observed_dynamic = json.loads(subprocess.check_output([str(inspector), str(candidate), "--dynamic"]))
            if observed_dynamic != expected_dynamic:
                raise RuntimeError(f"Dynamic tables or symbols differ from LLVM for {name}, stripped={stripped}")
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
        entries = {}
        start = programs[dynamic]["Offset"]
        for offset in range(start, start + programs[dynamic]["FileSize"], 16):
            key, value = struct.unpack_from("<QQ", original, offset)
            if key == 0:
                break
            entries[key] = (offset, value)
        changes = [("bad-string-size", entries[10][0] + 8, 0),
                   ("bad-symbol-size", entries[11][0] + 8, 16),
                   ("duplicate-string-table", entries[30][0], 5),
                   ("text-relocations", entries[30][0] + 8, 4),
                   ("unimplemented-symbol-versions", entries[30][0], 0x6ffffff0),
                   ("unimplemented-packed-relocations", entries[30][0], 0x60000011)]
        for label, offset, value in changes:
            contents = bytearray(original)
            struct.pack_into("<Q", contents, offset, value)
            candidate = build / f"{name}-{label}.elf"
            candidate.write_bytes(contents)
            check = subprocess.run([str(inspector), str(candidate), "--dynamic"], capture_output=True)
            if check.returncode != 1 or check.stdout:
                raise RuntimeError(f"Bad or unsupported dynamic metadata was not rejected: {name}/{label}")
            dynamic_rejected += 1
        if binary.read_bytes() != original:
            raise RuntimeError("Metadata inspection modified the original ELF")
        results.append({"file": binary.name, "sha256": hashlib.sha256(original).hexdigest(),
                        "view": expected, "dynamic": expected_dynamic})
    report = {"scope": "ELF data validation only; no guest execution", "ndk_revision": REVISION,
              "compiler": subprocess.check_output([str(clang), "--version"], text=True).splitlines()[0],
              "valid_views": len(results) * 2, "dynamic_views": len(results) * 2,
              "rejected_mutations": rejected, "rejected_dynamic_mutations": dynamic_rejected, "inputs": results}
    (Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "m2-metadata.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Dynamic ELF: {len(results) * 2} LLVM-matched program and symbol views, "
          f"{rejected + dynamic_rejected} malformed/unsupported inputs rejected; no guest execution")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ARTBox dynamic ELF: {error}", file=sys.stderr)
        sys.exit(1)
