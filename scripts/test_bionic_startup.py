"""Build real Bionic startup and a separately linked NDK allocator client.

Apple execution uses two signed wrappers and an explicitly bounded test load
group. Optional netd is absent; unexpected loader/thread calls fail the test.
This is not M2's complete threads/files/mmap acceptance suite.
"""
import sys
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from sources import obtain as obtain_source
from bionic_adapt import inventory
from dynamic_bundle import prepare

sys.path.insert(0, str(ROOT / "tools"))
from wrap_dynamic import pack_layout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    builds, artifacts = (Path(os.environ[key]) for key in ("ARTBOX_BUILD_DIR", "ARTBOX_ARTIFACTS_DIR"))
    build = args.build_dir.resolve() if args.build_dir else builds / "m2/bionic-startup"
    build.mkdir(parents=True, exist_ok=True)
    source = obtain_source("bionic")
    cutils = obtain_source("libcutils-headers")
    report = json.loads((artifacts / "m2-bionic-native.json").read_text(encoding="utf-8"))
    manifest = json.loads((ROOT / "third_party/bionic/m2-objects.json").read_text(encoding="utf-8"))
    libraries = json.loads((ROOT / "third_party/bionic/components.json").read_text(encoding="utf-8"))
    inputs = builds / "m2/bionic/native"
    partial = inputs / "bionic-m2-partial.o"
    if digest(partial) != report["partial_object_sha256"] or report["source_commit"] != manifest["source_commit"]:
        raise RuntimeError("Startup input differs from the verified native Bionic object")
    includes = ["-I", str(source / "libstdc++/include"), "-I", str(cutils / "libcutils/include"),
                "-I", str(ROOT / "third_party/bionic/adapters")]
    for name, selection in libraries["components"].items():
        directory = obtain_source(name)
        for relative in selection["includes"]:
            includes += ["-I", str(directory / relative)]
    for relative in ("libc", "libc/platform"):
        includes += ["-I", str(inputs / "overlay" / relative)]
    for relative in [".", "include", "platform", "private", "bionic", "async_safe/include",
                     "kernel/uapi/asm-arm64", "kernel/uapi", "kernel/android/uapi"] + manifest.get("includes", []):
        includes += ["-isystem" if relative == "include" else "-I", str(source / "libc" / relative)]
    ndk = obtain_ndk(args.ndk_root)
    host = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}[sys.platform]
    suffix = ".exe" if os.name == "nt" else ""
    tools = ndk / "toolchains/llvm/prebuilt" / host / "bin"

    def command(name, *words):
        subprocess.run([str(tools / (name + suffix)), *map(str, words)], check=True)

    bootstrap, client = build / "bootstrap.o", build / "client.o"
    command("clang++", *report["flags"], "-std=gnu++20", "-fno-exceptions", "-fno-rtti", "-nostdinc++",
            "-fno-stack-protector", "-ffreestanding", "-I", ROOT / "core/include", *includes,
            "-c", ROOT / "fixtures/bionic-startup/bootstrap.cpp", "-o", bootstrap)
    command("clang", "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-builtin",
            "-fstack-protector-strong", "-mstack-protector-guard=global", "-march=armv8-a", "-mno-outline-atomics",
            "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
            "-c", ROOT / "fixtures/bionic-startup/check.c", "-o", client)
    thread_source, thread_object = ROOT / "fixtures/bionic-startup/threads.c", build / "threads.o"
    command("clang", "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-builtin",
            "-fstack-protector-strong", "-mstack-protector-guard=global", "-march=armv8-a", "-mno-outline-atomics",
            "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
            "-c", thread_source, "-o", thread_object)
    futex_source, futex_object = ROOT / "fixtures/bionic-futex/check.c", build / "futex-check.o"
    command("clang", "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-stack-protector",
            "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
            "-c", futex_source, "-o", futex_object)
    file_source, file_object = ROOT / "fixtures/bionic-files/check.c", build / "file-check.o"
    command("clang", "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-builtin", "-fno-stack-protector",
            "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
            "-c", file_source, "-o", file_object)
    mapping_source, mapping_object = ROOT / "fixtures/bionic-files/mapping.c", build / "mapping-check.o"
    command("clang", "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC", "-fno-builtin", "-fno-stack-protector",
            "-mbranch-protection=none", "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
            "-c", mapping_source, "-o", mapping_object)
    # Bionic's priority-1 initializer must precede ordinary C++ constructors.
    # Pad writable storage to a complete native page for WriteProtected globals.
    script = (ROOT / "fixtures/bionic-dynamic/image.ld").read_text(encoding="utf-8")
    script = script.replace("KEEP(*(.init_array .init_array.*))",
                            "KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))")
    script = script.replace("*(COMMON)", "*(COMMON) . = ALIGN(16384);")
    linker_script = build / "image.ld"
    linker_script.write_text(script, encoding="utf-8")
    link = ["-shared", "--hash-style=both", "--build-id=none", "-z", "max-page-size=16384",
            "--pack-dyn-relocs=relr", "-T", linker_script]
    libc, app = build / "libc.so", build / "libstartup_client.so"
    command("ld.lld", *link, "-soname", libc.name, partial, bootstrap, "-o", libc)
    command("ld.lld", *link, "-soname", app.name, client, futex_object, thread_object, file_object, mapping_object, "--no-as-needed", libc, "-o", app)
    result = {"scope": "Real Bionic TLS/constructors/allocator in a controlled two-image test; not full M2",
              "source_commit": report["source_commit"], "partial_object_sha256": digest(partial),
              "bootstrap_source_sha256": digest(ROOT / "fixtures/bionic-startup/bootstrap.cpp"),
              "client_source_sha256": digest(ROOT / "fixtures/bionic-startup/check.c"), "images": {},
              "threads": {"source_sha256": digest(thread_source), "object_sha256": digest(thread_object),
                          "joined": 4, "detached": 2, "iterations_per_thread": 32},
              "rss_method": "Darwin getrusage RUSAGE_SELF ru_maxrss, bytes for the entire host process",
              "futex": {"cases": 19, "source_sha256": digest(futex_source), "object_sha256": digest(futex_object)},
              "mappings": {"cases": 43, "source_sha256": digest(mapping_source), "object_sha256": digest(mapping_object)},
              "files": {"cases": 41, "source_sha256": digest(file_source), "object_sha256": digest(file_object)}}
    notices = {name.upper() + "-NOTICE.txt": (inputs / (name.upper() + "-NOTICE.txt"), data["sha256"])
               for name, data in report["component_notices"].items()}
    notices["LIBCUTILS-NOTICE.txt"] = (inputs / "LIBCUTILS-NOTICE.txt", report["dependencies"]["libcutils-headers"]["notice_sha256"])
    notices["COMPILER-RT-NOTICE.txt"] = (inputs / "COMPILER-RT-NOTICE.txt", report["binary128"]["pin"]["notice_sha256"])
    binaries = {}
    for name, elf, framework_name in (("libc", libc, "ARTBoxBionic"), ("client", app, "ARTBoxStartupClient")):
        boundary = inventory(subprocess.check_output([str(tools / ("llvm-objdump" + suffix)), "-d", "--no-show-raw-insn", str(elf)], text=True))
        if any(boundary[key] for key in ("svc", "tpidr_mentions", "x18_mentions", "x27_mentions", "x28_mentions", "unknown_instructions")):
            raise RuntimeError("Startup ELF violates the native instruction boundary")
        _, _, layout = pack_layout(elf.read_bytes())
        packed = build / name / "pack"
        subprocess.run([sys.executable, "-B", str(ROOT / "tools/wrap_dynamic.py"), str(elf), str(packed)], check=True)
        data = {"elf_sha256": digest(elf), "layout": layout, "inventory": boundary}
        if sys.platform == "darwin":
            data["frameworks"] = {}
            for platform in ("macos", "ios"):
                binary, info = prepare(packed, build / name / platform, platform, inputs / "BIONIC-NOTICE.txt",
                                       report["notice_sha256"], notices, name=framework_name)
                data["frameworks"][platform] = info
                if platform == "macos":
                    binaries[name] = binary
        result["images"][name] = data
    if sys.platform == "darwin":
        (build / "input-manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        for key, options in (("native", []), ("sampled_native", ["--sampled"])):
            # Preserve disposable roots with the diagnostic artifact, even on failure.
            root = Path(tempfile.mkdtemp(prefix=key + "-root-", dir=build))
            (root / "data").mkdir()
            (root / "system").mkdir()
            process = subprocess.run([str(builds / "host/artbox_native_bionic_startup"), str(binaries["libc"]), str(libc),
                                      str(binaries["client"]), str(app), str(root), *options], capture_output=True, timeout=60)
            (build / (key + ".stdout")).write_bytes(process.stdout)
            (build / (key + ".stderr")).write_bytes(process.stderr)
            if process.returncode:
                print(process.stderr.decode("utf-8", errors="replace"), file=sys.stderr)
            process.check_returncode()
            result[key] = json.loads(process.stdout)
            if result[key]["cases"] != 146 or result[key]["futex_cases"] != 19:
                raise RuntimeError("NDK allocator client did not complete")
            if result[key]["file_cases"] != 41 or result[key]["mapping_cases"] != 43:
                raise RuntimeError("NDK regular-file client did not complete")
            if result[key]["pthread_result"] != 0 or result[key]["threads_reaped"] != 6:
                raise RuntimeError("NDK pthread client did not complete")
            if key == "sampled_native" and result[key]["thread_guarded_samples"] < 6:
                raise RuntimeError("GWP-ASan sampling did not reach every guest worker")
            if result[key]["process_peak_rss_bytes"] <= 0 or result[key]["pthread_client_ns"] <= 0:
                raise RuntimeError("Missing native thread or memory measurement")
    (artifacts / "m2-bionic-startup.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Bionic startup fixture built" + (" and executed through signed macOS wrappers" if sys.platform == "darwin" else "; Apple execution required"))


if __name__ == "__main__":
    main()
