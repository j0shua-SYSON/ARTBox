"""Compare ELF version definitions, requirements and lookups with real NDK/LLVM inputs."""
import sys
sys.dont_write_bytecode = True
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
from environment import ROOT, environment
from ndk import obtain


def main():
    os.environ.update(environment())
    builds = Path(os.environ["ARTBOX_BUILD_DIR"])
    build = builds / "m2/versions"
    build.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    inspector = next(p for p in (builds / f"host/artbox_inspect_elf{suffix}",
                     builds / f"host/Release/artbox_inspect_elf{suffix}") if p.is_file())
    platform = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    tools = obtain(None) / "toolchains/llvm/prebuilt" / platform / "bin"
    flags = [str(tools / f"clang{suffix}"), "--target=aarch64-linux-android35", "-shared", "-nostdlib",
             "-fPIC", "-O2", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wl,-z,max-page-size=16384"]
    results = []
    for hashing in ("gnu", "sysv", "both"):
        folder = build / hashing
        folder.mkdir(exist_ok=True)
        for name, source in (("versions", "versions.c"), ("version_client", "version_client.c")):
            elf = folder / f"lib{name}.so"
            command = flags + [f"-Wl,--hash-style={hashing}", f"-Wl,-soname,{elf.name}",
                               str(ROOT / "fixtures/dynamic" / source), "-o", str(elf)]
            if name == "versions":
                command += ["-Wl,--version-script," + str(ROOT / "fixtures/dynamic/versions.map")]
            else:
                command += ["-L", str(folder), "-lversions"]
            subprocess.run(command, check=True)
            raw = subprocess.check_output([str(tools / f"llvm-readobj{suffix}"), "--elf-output-style=JSON",
                                           "--version-info", "--dyn-symbols", str(elf)], text=True, encoding="utf-8")
            (folder / (name + "-llvm.txt")).write_text(raw, encoding="utf-8")
            # LLVM 19 emits this text-only field inside JSON definitions. These
            # fixtures have no predecessor entries; reject any different shape.
            normalized = re.sub(r"Predecessors: \[\]\s*", "", raw)
            reference = json.loads(normalized)[0]
            records = []
            for item in reference["VersionDefinitions"]:
                d = item["Definition"]
                records.append({"index": d["Index"], "flags": d["Flags"]["Value"], "hash": d["Hash"],
                                "name": d["Name"], "file": None})
            for item in reference["VersionRequirements"]:
                d = item["Dependency"]
                for entry in d["Entries"]:
                    e = entry["Entry"]
                    records.append({"index": e["Index"], "flags": e["Flags"]["Value"], "hash": e["Hash"],
                                    "name": e["Name"], "file": d["FileName"]})
            symbols = []
            for item, entry in zip(reference["VersionSymbols"], reference["DynamicSymbols"], strict=True):
                v, s = item["Symbol"], entry["Symbol"]
                record = next((r for r in records if r["index"] == v["Version"] and r["index"] > 1), None)
                symbols.append({"name": v["Name"].split("@", 1)[0], "index": v["Version"],
                                "hidden": int(bool(s["Section"]["Value"] and "@" in v["Name"] and "@@" not in v["Name"])),
                                "version": record["name"] if record else None, "file": record["file"] if record else None})
            expected = {"records": records, "symbols": symbols}
            original = elf.read_bytes()
            for stripped in (False, True):
                candidate = elf
                if stripped:
                    data = bytearray(original)
                    struct.pack_into("<Q", data, 40, 0)
                    struct.pack_into("<3H", data, 58, 0, 0, 0)
                    candidate = folder / (name + "-without-sections.elf")
                    candidate.write_bytes(data)
                observed = json.loads(subprocess.check_output([str(inspector), str(candidate), "--versions"]))
                if observed != expected:
                    raise RuntimeError(f"Version view differs from LLVM: {hashing}/{name}, stripped={stripped}\n{observed}\n{expected}")
            results.append({"hashing": hashing, "file": elf.name, "sha256": hashlib.sha256(original).hexdigest(),
                            "versions": expected})
    report = {"scope": "Version metadata and per-image lookups; not guest execution or dependency resolution",
              "validated_views": len(results) * 2, "inputs": results}
    (Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "m2-versions.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Version metadata and named export lookups: {len(results) * 2} LLVM-matched views")


if __name__ == "__main__":
    main()
