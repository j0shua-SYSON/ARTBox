"""Compare relocated NDK data with LLVM-expanded relocation records; no guest execution."""
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
    build = args.build_dir.resolve() if args.build_dir else builds / "m2/relocations"
    host = args.host_dir.resolve() if args.host_dir else builds / "host"
    suffix = ".exe" if os.name == "nt" else ""
    runner = next((p for p in (host / f"artbox_relocate_elf{suffix}",
                              host / "Release" / f"artbox_relocate_elf{suffix}") if p.is_file()), None)
    if runner is None:
        raise RuntimeError("Build the portable host before running relocation checks")
    ndk = obtain(args.ndk_root)
    platform = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    tools = ndk / "toolchains/llvm/prebuilt" / platform / "bin"
    build.mkdir(parents=True, exist_ok=True)
    clang = tools / f"clang{suffix}"
    flags = [str(clang), "--target=aarch64-linux-android35", "-std=c11", "-fPIC", "-shared", "-nostdlib",
             "-Wl,-z,max-page-size=16384", "-Wl,--hash-style=both"]
    cases = []
    for packed in (True, False):
        directory = build / ("relr" if packed else "rela")
        directory.mkdir(parents=True, exist_ok=True)
        files, references, originals = {}, {}, {}
        for name in ("dependency", "relocations"):
            binary = directory / f"libartbox_{name}.so"
            command = flags + [str(ROOT / f"fixtures/dynamic/{name}.c"), f"-Wl,-soname,{binary.name}",
                               f"-Wl,--pack-dyn-relocs={'relr' if packed else 'none'}", "-o", str(binary)]
            if name == "relocations":
                command += ["-L", str(directory), "-lartbox_dependency"]
            subprocess.run(command, check=True)
            originals[name] = binary.read_bytes()
            references[name] = json.loads(subprocess.check_output([
                str(tools / f"llvm-readobj{suffix}"), "--elf-output-style=JSON", "--program-headers",
                "--dyn-symbols", "--relocations", "--expand-relocs", str(binary)]))[0]
            files[name] = binary
        reference, original = references["relocations"], originals["relocations"]
        loads = [p["ProgramHeader"] for p in reference["ProgramHeaders"] if p["ProgramHeader"]["Type"]["Value"] == 1]
        expected = {}
        for index, p in enumerate(loads):
            if p["Flags"]["Value"] & 2:
                expected[index] = bytearray(original[p["Offset"]:p["Offset"] + p["FileSize"]])
                expected[index] += bytes(p["MemSize"] - p["FileSize"])
        symbols = {}
        for name, bias in (("relocations", 0x10000000), ("dependency", 0x20000000)):
            for entry in references[name]["DynamicSymbols"]:
                symbol = entry["Symbol"]
                if symbol["Section"]["Value"] and symbol["Binding"]["Value"] in (1, 2):
                    address = symbol["Value"] + (0 if symbol["Section"]["Value"] == 0xfff1 else bias)
                    symbols.setdefault(symbol["Name"]["Name"], address)
        requested = [s["Symbol"] for s in reference["DynamicSymbols"]]
        counts = {"rela": 0, "plt": 0, "relr": 0}
        for section in reference["Relocations"]:
            for entry in section["Relocs"]:
                relocation = entry["Relocation"]
                address, kind = relocation["Offset"], relocation["Type"]["Value"]
                index = next((i for i in expected if loads[i]["VirtualAddress"] <= address and
                              address + 8 <= loads[i]["VirtualAddress"] + loads[i]["MemSize"]), None)
                if index is None:
                    raise RuntimeError("LLVM relocation target is outside writable data")
                offset = address - loads[index]["VirtualAddress"]
                if kind == 1027:
                    addend = relocation.get("Addend", struct.unpack_from("<Q", expected[index], offset)[0])
                    value = 0x10000000 + addend
                    counts["rela" if "Addend" in relocation else "relr"] += 1
                elif kind in (257, 1025, 1026):
                    symbol = requested[relocation["Symbol"]["Value"]]
                    name = symbol["Name"]["Name"]
                    if name not in symbols and symbol["Binding"]["Value"] != 2:
                        raise RuntimeError(f"Unexpected unresolved fixture import: {name}")
                    value = symbols.get(name, 0) + relocation["Addend"]
                    counts["plt" if kind == 1026 else "rela"] += 1
                else:
                    raise RuntimeError(f"Unexpected fixture relocation: {relocation['Type']['Name']}")
                struct.pack_into("<Q", expected[index], offset, value & ((1 << 64) - 1))
        required = {"rela": 5 if packed else 135, "plt": 1, "relr": 130 if packed else 0}
        if counts != required:
            raise RuntimeError(f"Fixture lost required relocation coverage: {counts}")
        expected_output = {"bias": 0x10000000, **counts,
                           "segments": [{"index": i, "bytes": data.hex()} for i, data in expected.items()]}
        for stripped in (False, True):
            candidate = files["relocations"]
            if stripped:
                contents = bytearray(original)
                struct.pack_into("<Q", contents, 40, 0)
                struct.pack_into("<3H", contents, 58, 0, 0, 0)
                candidate = directory / "without-sections.elf"
                candidate.write_bytes(contents)
            observed = json.loads(subprocess.check_output([str(runner), str(candidate), str(files["dependency"])]))
            if observed != expected_output:
                raise RuntimeError(f"Relocated data differs from LLVM: packed={packed}, stripped={stripped}")
        for name, path in files.items():
            if path.read_bytes() != originals[name]:
                raise RuntimeError("Relocation checks changed an original ELF")
        cases.append({"encoding": "RELR" if packed else "RELA", "counts": counts,
                      "sha256": hashlib.sha256(original).hexdigest(), "sectionless_equal": True,
                      "writable_segments": [{"index": i, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                                            for i, data in expected.items()]})
    report = {"scope": "Data relocation only; no guest execution or Bionic acceptance", "ndk_revision": REVISION,
              "compiler": subprocess.check_output([str(clang), "--version"], text=True).splitlines()[0], "cases": cases}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-relocations.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Four NDK data views match LLVM: 136 relocations each; RELA, RELR, GOT, PLT and weak import")


if __name__ == "__main__":
    main()
