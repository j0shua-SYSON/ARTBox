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
import shutil
import subprocess

from environment import ROOT, environment
from ndk import REVISION, obtain as obtain_ndk
from sources import obtain as obtain_source
from bionic_adapt import adapt_sources, check_native, inventory, stack_references
from bionic_syscalls import generate as generate_syscalls
from bionic_builtins import prepare as prepare_builtins


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--profile", choices=("upstream", "native"), default="native")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    os.environ.update(environment())
    build = args.build_dir.resolve() if args.build_dir else Path(os.environ["ARTBOX_BUILD_DIR"]) / "m2/bionic" / args.profile
    build.mkdir(parents=True, exist_ok=True)
    manifest_path = ROOT / "third_party/bionic/m2-objects.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    pin = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))["bionic"]
    if manifest["source_commit"] != pin["commit"]:
        raise RuntimeError("Bionic source selection does not match the pinned source revision")
    source = obtain_source("bionic")
    cutils = obtain_source("libcutils-headers")
    cutils_pin = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))["libcutils-headers"]
    component_path = ROOT / "third_party/bionic/components.json"
    libraries = json.loads(component_path.read_text(encoding="utf-8"))
    source_pins = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))
    components = {}
    for name, selection in libraries["components"].items():
        if selection["source_commit"] != source_pins[name]["commit"]:
            raise RuntimeError(f"Component selection differs from source pin: {name}")
        components[name] = obtain_source(name)
    adaptations, overlay = [], None
    if args.profile == "native":
        patch = json.loads((ROOT / "third_party/bionic/native-boundary.json").read_text(encoding="utf-8"))
        if patch["source_commit"] != pin["commit"]:
            raise RuntimeError("Bionic adaptation does not match the pinned revision")
        overlay = build / "overlay"
        adaptations = adapt_sources(source, overlay, patch)
    generated, syscall_info = generate_syscalls(source, build / "generated", args.profile)
    if syscall_info["pin"]["source_commit"] != pin["commit"] or \
            manifest.get("generated_sources") != ["generated/syscalls-arm64.S"]:
        raise RuntimeError("Bionic syscall selection does not match the source manifest")
    ndk = obtain_ndk(args.ndk_root)
    suffix = ".exe" if os.name == "nt" else ""
    platform = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    tools = ndk / "toolchains/llvm/prebuilt" / platform / "bin"
    # Bionic's libc_defaults selects no C++ standard library. Soong at the same
    # Android tag selects gnu++20 and these warning exceptions.
    # Keep -Werror; do not alter upstream source to compensate for missing flags.
    abi_flags = ["--target=aarch64-linux-android35", "-fPIC", "-mbranch-protection=none",
                 "-ffixed-x18", "-ffixed-x27", "-ffixed-x28"]
    assembly_flags = ["--target=aarch64-linux-android35", "-fPIC", "-march=armv8-a",
                      "-mbranch-protection=none", "-D_LIBC=1", "-D__ASSEMBLY__"]
    native_flags = ["-DARTBOX_NATIVE_HOST=1", "-march=armv8-a", "-mno-outline-atomics",
                    "-mstack-protector-guard=global"] if args.profile == "native" else []
    flags = ["--target=aarch64-linux-android35", "-O2", "-fPIC", "-D_LIBC=1", "-DANDROID",
             "-D__BIONIC_LP32_USE_STAT64", "-DUSE_SCUDO", "-fno-builtin",
             "-fno-emulated-tls", "-fstack-protector-strong", "-mbranch-protection=none",
             "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Wunused", "-Werror",
             "-Wno-char-subscripts", "-Wno-deprecated-declarations", "-Wno-gcc-compat",
             "-Wno-reorder-init-list", "-Wno-non-c-typedef-for-linkage", "-Wframe-larger-than=2048",
             "-Wno-missing-field-initializers", "-Wno-vla-cxx-extension", "-Wno-c99-designator",
             "-Werror=pointer-to-int-cast", "-Werror=int-to-pointer-cast", "-Werror=type-limits",
             "-Wexit-time-destructors"]
    if args.profile == "native":
        flags += native_flags
        # GWP-ASan supplies a platform hook so its initial-exec ELF TLS need
        # not read TPIDR_EL0. All users of this inline header share the hook.
        flags += ['-DGWP_ASAN_PLATFORM_TLS_HEADER="artbox_gwp_asan_tls.h"']
    language_flags = {".cpp": ["-std=gnu++20", "-fno-exceptions", "-fno-rtti", "-nostdinc++"],
                      ".c": ["-std=gnu99"], ".S": []}
    includes = ["-I", str(source / "libstdc++/include"), "-I", str(cutils / "libcutils/include")]
    includes += ["-I", str(ROOT / "third_party/bionic/adapters")]
    for name, component in components.items():
        for relative in libraries["components"][name]["includes"]:
            includes += ["-I", str(component / relative)]
    if overlay:
        includes += ["-I", str(overlay / "libc"), "-I", str(overlay / "libc/platform")]
    for relative in (".", "include", "platform", "private", "bionic", "async_safe/include",
                     "kernel/uapi/asm-arm64", "kernel/uapi", "kernel/android/uapi"):
        includes += ["-isystem" if relative == "include" else "-I", str(source / "libc" / relative)]
    for relative in manifest.get("includes", []):
        includes += ["-I", str(source / "libc" / relative)]
    compiler = tools / f"clang++{suffix}"
    entries = []
    for relative in manifest["sources"]:
        original = source / relative
        if not original.resolve().is_relative_to(source.resolve()) or not original.is_file():
            raise RuntimeError(f"Invalid Bionic source selection: {relative}")
        expected = manifest.get("art_runtime_sources", {}).get(relative)
        if expected is not None and digest(original) != expected:
            raise RuntimeError(f"ART Bionic dependency source changed: {relative}")
        path = overlay / relative if any(a["path"] == relative for a in adaptations) else original
        output = build / "objects" / (relative + ".o")
        output.parent.mkdir(parents=True, exist_ok=True)
        entries.append((relative, path, output, None))
    generated_object = build / "objects/generated/syscalls-arm64.S.o"
    generated_object.parent.mkdir(parents=True, exist_ok=True)
    entries.append(("generated/syscalls-arm64.S", generated, generated_object, None))
    for name, component in components.items():
        selection = libraries["components"][name]
        component_flags = abi_flags + native_flags + libraries["common_flags"] + selection["flags"]
        if args.profile == "native":
            component_flags += ['-DGWP_ASAN_PLATFORM_TLS_HEADER="artbox_gwp_asan_tls.h"']
        for relative in selection["sources"]:
            path = component / relative
            if not path.resolve().is_relative_to(component.resolve()) or not path.is_file():
                raise RuntimeError(f"Invalid component source selection: {name}/{relative}")
            output = build / "objects" / name / (relative + ".o")
            output.parent.mkdir(parents=True, exist_ok=True)
            applied = assembly_flags + selection["flags"] if path.suffix == ".S" else component_flags
            entries.append((name + "/" + relative, path, output, applied))

    def compile_one(entry):
        relative, path, output, component_flags = entry
        if path.suffix not in language_flags:
            raise RuntimeError(f"Unsupported Bionic source language: {path.suffix}")
        source_flags = manifest.get("source_flags", {}).get(relative, [])
        driver = compiler if path.suffix == ".cpp" else tools / f"clang{suffix}"
        applied_flags = component_flags if component_flags is not None else (assembly_flags if path.suffix == ".S" else flags)
        command = [str(driver), *applied_flags, *language_flags[path.suffix], *source_flags, *includes, "-c", str(path), "-o", str(output)]
        result = subprocess.run(command, capture_output=True)
        output.with_suffix(".log").write_bytes(result.stdout + result.stderr)
        if result.returncode:
            return {"source": relative, "exit": result.returncode, "source_sha256": digest(path)}
        return {"source": relative, "exit": 0, "source_sha256": digest(path), "object_sha256": digest(output),
                "source_flags": source_flags, "language_flags": language_flags[path.suffix], "flags": applied_flags}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled = list(pool.map(compile_one, entries))
    (build / "compilation.json").write_text(json.dumps(compiled, indent=2) + "\n", encoding="utf-8")
    failed = [r["source"] for r in compiled if r["exit"]]
    if failed:
        raise RuntimeError(f"Bionic compilation failed for {', '.join(failed)}; see per-source logs in {build}")

    # Run these exact assembled stubs and the real Bionic errno helper on Linux.
    # Prefix only the test copy so no definition can interpose on the host libc.
    test_names = {"generated/syscalls-arm64.S", "libc/arch-arm64/bionic/syscall.S", "libc/bionic/__set_errno.cpp"}
    test_inputs = [entry[2] for entry in entries if entry[0] in test_names]
    if len(test_inputs) != 3:
        raise RuntimeError("Bionic syscall oracle inputs are incomplete")
    test_unprefixed, test_object = build / "syscall-test-unprefixed.o", build / "syscall-test.o"
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-r", *map(str, test_inputs), "-o", str(test_unprefixed)], check=True)
    subprocess.run([str(tools / f"llvm-objcopy{suffix}"), "--prefix-symbols=artbox_stub_", str(test_unprefixed), str(test_object)], check=True)
    syscall_info["test_object_sha256"] = digest(test_object)

    # Keep the exact production string implementations and dispatcher together;
    # prefix only this test copy to prevent host-libc interposition.
    string_inputs = [entry[2] for entry in entries if entry[0].startswith("arm-routines/") or
                     entry[0] == "libc/arch-arm64/static_function_dispatch.S"]
    if len(string_inputs) != 15:
        raise RuntimeError("The baseline ARM64 string oracle inputs are incomplete")
    strings_raw, strings_prefixed = build / "strings-unprefixed.o", build / "strings-prefixed.o"
    string_check, string_test = build / "strings-check.o", build / "strings-test.o"
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-r", *map(str, string_inputs), "-o", str(strings_raw)], check=True)
    subprocess.run([str(tools / f"llvm-objcopy{suffix}"), "--prefix-symbols=artbox_string_",
                    str(strings_raw), str(strings_prefixed)], check=True)
    # This standalone oracle has no libc/TLS dependency, including a stack guard.
    subprocess.run([str(tools / f"clang{suffix}"), *abi_flags, "-std=c11", "-O2", "-fno-builtin",
                    "-fno-stack-protector", "-Wall", "-Wextra", "-Werror", "-c",
                    str(ROOT / "fixtures/bionic-strings/check.c"), "-o", str(string_check)], check=True)
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-r", str(strings_prefixed), str(string_check),
                    "-o", str(string_test)], check=True)
    if subprocess.check_output([str(tools / f"llvm-nm{suffix}"), "--undefined-only", str(string_test)]).strip():
        raise RuntimeError("The string oracle unexpectedly depends on host library functions")
    string_inventory = inventory(subprocess.check_output([str(tools / f"llvm-objdump{suffix}"), "-d",
                                                          "--no-show-raw-insn", str(string_test)], text=True))
    if any(string_inventory[key] for key in ("svc", "tpidr_mentions", "x18_mentions", "x27_mentions", "x28_mentions", "unknown_instructions")):
        raise RuntimeError("String routines or their oracle violate the native instruction boundary")
    strings = {"object_sha256": digest(string_test), "inventory": string_inventory, "cases": 35908,
               "check_sha256": digest(ROOT / "fixtures/bionic-strings/check.c"),
               "source_commit": source_pins["arm-routines"]["commit"]}

    allocator_test = build / "allocator-tls-test.o"
    subprocess.run([str(compiler), *flags, *language_flags[".cpp"], *includes, "-c",
                    str(ROOT / "fixtures/bionic-allocator-tls/access.cpp"), "-o", str(allocator_test)], check=True)
    allocator_disassembly = subprocess.check_output([str(tools / f"llvm-objdump{suffix}"), "-d",
                                                     "--no-show-raw-insn", str(allocator_test)], text=True)
    allocator_tls = {"object_sha256": digest(allocator_test), "inventory": inventory(allocator_disassembly),
                     "adapter_sha256": digest(ROOT / "third_party/bionic/adapters/artbox_gwp_asan_tls.h")}
    if args.profile == "native" and any(allocator_tls["inventory"][key] for key in
                                       ("svc", "tpidr_mentions", "x18_mentions", "x27_mentions", "x28_mentions", "unknown_instructions")):
        raise RuntimeError("Allocator TLS test caller violates the native instruction boundary")

    # A relocatable link verifies these objects agree on their shared symbols.
    # Remaining undefined symbols are required dependencies, never zero stubs.
    combined = build / "bionic-m2-partial.o"
    builtins, binary128 = prepare_builtins(tools, build)
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-r", *[str(e[2]) for e in entries],
                    *map(str, builtins), "-o", str(combined)], check=True)
    structure = json.loads(subprocess.check_output([str(tools / f"llvm-readobj{suffix}"), "--elf-output-style=JSON",
                                                   "--file-headers", "--symbols", "--relocations", str(combined)]))[0]
    header = structure["ElfHeader"]
    if not header["Type"].endswith("(0x1)") or header["Machine"]["Value"] != 183:
        raise RuntimeError("Bionic objects did not link to an AArch64 relocatable ELF")
    named = {entry["Symbol"]["Name"]["Name"]: entry["Symbol"] for entry in structure["Symbols"]}
    setter = named.get("__set_tls")
    if not setter or setter["Binding"]["Value"] != 1 or setter["Type"]["Value"] != 2 or \
            setter["Other"]["Value"] & 3 != 2 or not setter["Section"]["Value"]:
        raise RuntimeError("Bionic __set_tls must remain a defined hidden function")
    if args.profile == "native":
        for name in ("artbox_bionic_get_tls", "artbox_bionic_set_tls", "artbox_bionic_syscall"):
            entry = named.get(name)
            if not entry or entry["Section"]["Value"] != 0 or entry["Other"]["Value"] & 3:
                raise RuntimeError(f"Bionic host endpoint must be an ordinary unresolved import: {name}")
    undefined_text = subprocess.check_output([str(tools / f"llvm-nm{suffix}"), "--undefined-only", "--format=posix",
                                              "--no-demangle", str(combined)], text=True)
    undefined = [{"symbol": line.split()[0], "kind": line.split()[1]} for line in undefined_text.splitlines() if line.strip()]
    defined_text = subprocess.check_output([str(tools / f"llvm-nm{suffix}"), "--defined-only", "--extern-only",
                                            "--format=posix", "--no-demangle", str(combined)], text=True)
    defined = sorted(line.split()[0] for line in defined_text.splitlines() if line.strip())
    disassembly = subprocess.check_output([str(tools / f"llvm-objdump{suffix}"), "-d", "--no-show-raw-insn", str(combined)], text=True)
    (build / "disassembly.txt").write_text(disassembly, encoding="utf-8")
    boundaries = inventory(disassembly)
    protection = stack_references(structure["Relocations"])
    if args.profile == "native":
        check_native(boundaries, undefined, protection)
    notice = build / "BIONIC-NOTICE.txt"
    shutil.copyfile(source / pin["notice"], notice)
    if digest(notice) != pin["notice_sha256"]:
        raise RuntimeError("Bionic notice changed during compilation")
    cutils_notice = build / "LIBCUTILS-NOTICE.txt"
    shutil.copyfile(cutils / cutils_pin["notice"], cutils_notice)
    if digest(cutils_notice) != cutils_pin["notice_sha256"]:
        raise RuntimeError("libcutils notice changed during compilation")
    component_notices = {}
    for name, component in components.items():
        component_pin = source_pins[name]
        parts = {component_pin["notice"]: component_pin["notice_sha256"],
                 **libraries["components"][name].get("additional_notices", {})}
        notice_data = bytearray()
        for relative, expected in parts.items():
            path = component / relative
            if digest(path) != expected:
                raise RuntimeError(f"Component notice changed: {name}/{relative}")
            notice_data.extend(f"\n--- {name}/{relative} ---\n".encode("utf-8"))
            notice_data.extend(path.read_bytes())
        notice_path = build / (name.upper() + "-NOTICE.txt")
        notice_path.write_bytes(notice_data)
        component_notices[name] = {"sha256": digest(notice_path), "parts": parts}
    inline_raise = build / "bionic_inline_raise.h"
    shutil.copyfile((overlay or source) / "libc/private/bionic_inline_raise.h", inline_raise)
    report = {"scope": "Partial source compilation and ABI inventory; no libc.so, guest execution or M2 acceptance",
              "profile": args.profile, "source_commit": pin["commit"], "selection_sha256": digest(manifest_path), "ndk_revision": REVISION,
              "compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
              "flags": flags, "compiled": compiled, "adaptations": adaptations,
              "defined_symbols": defined, "partial_object_sha256": digest(combined),
              "weak_definitions": sorted(name for name, entry in named.items()
                                         if entry["Binding"]["Value"] == 2 and entry["Section"]["Value"]),
              "tls_definitions": {name: entry["Size"] for name, entry in named.items()
                                  if entry["Type"]["Value"] == 6 and entry["Section"]["Value"]},
              "notice_sha256": digest(notice), "undefined_symbols": undefined, "native_boundary_inventory": boundaries,
              "stack_protection": protection, "dependencies": {"libcutils-headers": cutils_pin,
                  **{name: source_pins[name] for name in components}},
              "component_selection_sha256": digest(component_path), "component_notices": component_notices,
              "allocator_tls": allocator_tls, "strings": strings, "binary128": binary128,
              "inline_raise_sha256": digest(inline_raise), "syscall_stubs": syscall_info}
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / f"m2-bionic-{args.profile}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Compiled and combined {len(compiled)} pinned Bionic sources; {len(undefined)} unresolved dependencies")
    print(f"{args.profile} object instruction inventory: {boundaries}")


if __name__ == "__main__":
    main()
