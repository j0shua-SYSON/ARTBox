"""Package real Bionic syscall objects in a controlled dynamic ELF layout.

The small original probe exercises data/BSS/constructor addressing. This is a
loader fixture, not a complete Bionic runtime or M2 acceptance result.
"""
import sys
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from environment import ROOT, environment
from ndk import obtain
from bionic_adapt import inventory
from dynamic_bundle import prepare

sys.path.insert(0, str(ROOT / "tools"))
from wrap_dynamic import pack_layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    builds = Path(os.environ["ARTBOX_BUILD_DIR"])
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    build = args.build_dir.resolve() if args.build_dir else builds / "m2/dynamic-wrapper"
    build.mkdir(parents=True, exist_ok=True)
    report = json.loads((artifacts / "m2-bionic-native.json").read_text(encoding="utf-8"))
    source_object = builds / "m2/bionic/native/syscall-test.o"
    if hashlib.sha256(source_object.read_bytes()).hexdigest() != report["syscall_stubs"]["test_object_sha256"]:
        raise RuntimeError("Dynamic wrapper input differs from the verified Bionic syscall slice")
    strings = builds / "m2/bionic/native/strings-test.o"
    if hashlib.sha256(strings.read_bytes()).hexdigest() != report["strings"]["object_sha256"]:
        raise RuntimeError("Dynamic wrapper input differs from the verified Bionic string slice")
    binary128 = builds / "m2/bionic/native/binary128-test.o"
    if hashlib.sha256(binary128.read_bytes()).hexdigest() != report["binary128"]["object_sha256"]:
        raise RuntimeError("Dynamic wrapper arithmetic differs from the reviewed Android compiler runtime")
    ndk = obtain(args.ndk_root)
    host = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    suffix = ".exe" if os.name == "nt" else ""
    tools = ndk / "toolchains/llvm/prebuilt" / host / "bin"
    clang = tools / f"clang{suffix}"
    probe, elf = build / "probe.o", build / "libartbox_bionic_slice.so"
    subprocess.run([str(clang), "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-stack-protector",
                    "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
                    "-c", str(ROOT / "fixtures/bionic-dynamic/probe.c"), "-o", str(probe)], check=True)
    vm_source, vm_object = ROOT / "fixtures/bionic-vm/check.c", build / "vm-check.o"
    subprocess.run([str(clang), "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-stack-protector",
                    "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
                    "-c", str(vm_source), "-o", str(vm_object)], check=True)
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-shared", "--hash-style=both", "--build-id=none",
                    "-z", "max-page-size=16384", "--pack-dyn-relocs=relr", "-soname", elf.name,
                    "-T", str(ROOT / "fixtures/bionic-dynamic/image.ld"), str(source_object), str(strings), str(probe), str(vm_object),
                    str(binary128), "-o", str(elf)], check=True)
    disassembly = subprocess.check_output([str(tools / f"llvm-objdump{suffix}"), "-d", "--no-show-raw-insn", str(elf)], text=True)
    boundary = inventory(disassembly)
    if any(boundary[key] for key in ("svc", "tpidr_mentions", "x18_mentions", "x27_mentions", "x28_mentions", "unknown_instructions")):
        raise RuntimeError("Linked Bionic slice violates the native instruction boundary")
    inspector = next((p for p in (builds / "host" / f"artbox_inspect_elf{suffix}",
                                  builds / "host/Release" / f"artbox_inspect_elf{suffix}") if p.is_file()), None)
    if inspector is None:
        raise RuntimeError("Build the portable host before dynamic wrapper checks")
    metadata = subprocess.check_output([str(inspector), str(elf)], text=True)
    (build / "elf-metadata.json").write_text(metadata, encoding="utf-8")
    _, _, layout = pack_layout(elf.read_bytes())
    subprocess.run([sys.executable, "-B", str(ROOT / "tools/wrap_dynamic.py"), str(elf), str(build / "pack")], check=True)
    result = {"scope": "Controlled dynamic Bionic syscall slice; no complete libc startup",
              "source_commit": report["source_commit"], "source_object_sha256": report["syscall_stubs"]["test_object_sha256"],
              "strings": report["strings"],
              "binary128": report["binary128"],
              "vm": {"cases": 35, "object_sha256": hashlib.sha256(vm_object.read_bytes()).hexdigest(),
                     "source_sha256": hashlib.sha256(vm_source.read_bytes()).hexdigest()},
              "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(), "layout": layout, "inventory": boundary}
    if sys.platform == "darwin":
        result["frameworks"] = {}
        for target in ("macos", "ios"):
            binary, framework = prepare(build / "pack", build / target, target,
                                        builds / "m2/bionic/native/BIONIC-NOTICE.txt", report["notice_sha256"],
                                        {"ARM-ROUTINES-NOTICE.txt": (builds / "m2/bionic/native/ARM-ROUTINES-NOTICE.txt",
                                                                    report["component_notices"]["arm-routines"]["sha256"]),
                                         "COMPILER-RT-NOTICE.txt": (builds / "m2/bionic/native/COMPILER-RT-NOTICE.txt",
                                                                    report["binary128"]["pin"]["notice_sha256"])})
            if target == "macos":
                runner = builds / "host/artbox_native_dynamic"
                output = subprocess.check_output([str(runner), str(binary), str(elf)], timeout=30)
                native = json.loads(output)
                if any(native[key] != value for key, value in
                       {"iterations": 100, "writes": 200, "exit_status": 0, "constructor_runs": 1,
                        "string_cases": 35908, "vm_cases": 35, "binary128_cases": 123}.items()):
                    raise RuntimeError("Signed dynamic Bionic slice did not complete its native contract")
                framework["native"] = native
            result["frameworks"][target] = framework
    (artifacts / "m2-dynamic-wrapper.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Dynamic Bionic slice: signed native Mac execution and iOS framework verification passed" if sys.platform == "darwin" else
          "NDK Bionic slice linked, instruction checked and packed; Apple linking/runtime checks still required")


if __name__ == "__main__":
    main()
