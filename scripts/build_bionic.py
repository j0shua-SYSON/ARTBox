"""Compile pinned Bionic sources and inventory unresolved native boundaries.

This builds partial ELF objects, not libc.so or an executable Apple package.
"""
import sys

sys.dont_write_bytecode = True

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from environment import ROOT, environment
from ndk import REVISION, obtain as obtain_ndk
from sources import obtain as obtain_source


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    os.environ.update(environment())
    build = args.build_dir.resolve() if args.build_dir else Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/bionic"
    build.mkdir(parents=True, exist_ok=True)
    manifest_path = ROOT / "third_party/bionic/m2-objects.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    pin = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))["bionic"]
    if manifest["source_commit"] != pin["commit"]:
        raise RuntimeError("Bionic source selection does not match the pinned source revision")
    source = obtain_source("bionic")
    ndk = obtain_ndk(args.ndk_root)
    suffix = ".exe" if os.name == "nt" else ""
    platform = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    tools = ndk / "toolchains/llvm/prebuilt" / platform / "bin"
    # Bionic's libc_defaults selects no C++ standard library. Soong at the same
    # Android tag selects gnu++20 and this anonymous-typedef warning exception.
    # Keep -Werror; do not alter upstream source to compensate for missing flags.
    flags = ["--target=aarch64-linux-android35", "-std=gnu++20", "-O2", "-fPIC", "-D_LIBC=1",
             "-D__BIONIC_LP32_USE_STAT64", "-DUSE_SCUDO", "-fno-builtin", "-fno-exceptions", "-fno-rtti",
             "-fno-emulated-tls", "-fstack-protector-strong", "-nostdinc++", "-mbranch-protection=none",
             "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Wunused", "-Werror",
             "-Wno-char-subscripts", "-Wno-deprecated-declarations", "-Wno-gcc-compat",
             "-Wno-reorder-init-list", "-Wno-non-c-typedef-for-linkage", "-Wframe-larger-than=2048",
             "-Werror=pointer-to-int-cast", "-Werror=int-to-pointer-cast", "-Werror=type-limits",
             "-Wexit-time-destructors"]
    includes = ["-I", str(source / "libstdc++/include")]
    for relative in (".", "include", "platform", "private", "bionic", "async_safe/include",
                     "kernel/uapi/asm-arm64", "kernel/uapi", "kernel/android/uapi"):
        includes += ["-isystem" if relative == "include" else "-I", str(source / "libc" / relative)]
    compiler = tools / f"clang++{suffix}"
    entries = []
    for relative in manifest["sources"]:
        path = source / relative
        if not path.resolve().is_relative_to(source.resolve()) or not path.is_file():
            raise RuntimeError(f"Invalid Bionic source selection: {relative}")
        output = build / "objects" / (relative + ".o")
        output.parent.mkdir(parents=True, exist_ok=True)
        entries.append((relative, path, output))

    def compile_one(entry):
        relative, path, output = entry
        command = [str(compiler), *flags, *includes, "-c", str(path), "-o", str(output)]
        result = subprocess.run(command, capture_output=True)
        output.with_suffix(".log").write_bytes(result.stdout + result.stderr)
        if result.returncode:
            return {"source": relative, "exit": result.returncode, "source_sha256": digest(path)}
        return {"source": relative, "exit": 0, "source_sha256": digest(path), "object_sha256": digest(output)}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled = list(pool.map(compile_one, entries))
    (build / "compilation.json").write_text(json.dumps(compiled, indent=2) + "\n", encoding="utf-8")
    failed = [r["source"] for r in compiled if r["exit"]]
    if failed:
        raise RuntimeError(f"Bionic compilation failed for {', '.join(failed)}; see per-source logs in {build}")

    # A relocatable link verifies these objects agree on their shared symbols.
    # Remaining undefined symbols are required dependencies, never zero stubs.
    combined = build / "bionic-m2-partial.o"
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-r", *[str(e[2]) for e in entries], "-o", str(combined)], check=True)
    header = json.loads(subprocess.check_output([str(tools / f"llvm-readobj{suffix}"), "--elf-output-style=JSON",
                                                "--file-headers", str(combined)]))[0]["ElfHeader"]
    if not header["Type"].endswith("(0x1)") or header["Machine"]["Value"] != 183:
        raise RuntimeError("Bionic objects did not link to an AArch64 relocatable ELF")
    undefined_text = subprocess.check_output([str(tools / f"llvm-nm{suffix}"), "--undefined-only", "--format=posix",
                                              "--no-demangle", str(combined)], text=True)
    undefined = [{"symbol": line.split()[0], "kind": line.split()[1]} for line in undefined_text.splitlines() if line.strip()]
    disassembly = subprocess.check_output([str(tools / f"llvm-objdump{suffix}"), "-d", "--no-show-raw-insn", str(combined)], text=True)
    (build / "disassembly.txt").write_text(disassembly, encoding="utf-8")
    patterns = {"svc": r"\bsvc\s+", "tpidr_el0_read": r"\bmrs\s+[^,\n]+,\s*tpidr_el0\b",
                "tpidr_el0_write": r"\bmsr\s+tpidr_el0\b", "x18_mentions": r"\b[wx]18\b"}
    boundaries = {name: len(re.findall(pattern, disassembly, re.IGNORECASE)) for name, pattern in patterns.items()}
    notice = build / "BIONIC-NOTICE.txt"
    shutil.copyfile(source / pin["notice"], notice)
    if digest(notice) != pin["notice_sha256"]:
        raise RuntimeError("Bionic notice changed during compilation")
    report = {"scope": "Partial source compilation and ABI inventory; no libc.so, guest execution or M2 acceptance",
              "source_commit": pin["commit"], "selection_sha256": digest(manifest_path), "ndk_revision": REVISION,
              "compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
              "flags": flags, "compiled": compiled, "partial_object_sha256": digest(combined),
              "notice_sha256": digest(notice), "undefined_symbols": undefined, "native_boundary_inventory": boundaries}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / "m2-bionic-objects.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Compiled and combined {len(compiled)} pinned Bionic sources; {len(undefined)} unresolved dependencies")
    print(f"Native boundary inventory (not yet adapted): {boundaries}")


if __name__ == "__main__":
    main()
