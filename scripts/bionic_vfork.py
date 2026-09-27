"""Build a closed oracle from the production Bionic vfork and errno objects."""
import hashlib
import os
import subprocess

from environment import ROOT
from bionic_adapt import inventory


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(tools, build, source, overlay, entries, assembly_flags, includes):
    suffix = ".exe" if os.name == "nt" else ""

    def run(name, *args):
        return subprocess.check_output([str(tools / (name + suffix)), *map(str, args)])

    def inspect(path):
        return inventory(run("llvm-objdump", "-d", "--no-show-raw-insn", path).decode())

    objects = {name: obj for name, _, obj, _ in entries}
    relative = "libc/arch-arm64/bionic/vfork.S"
    directory = build / "vfork"
    directory.mkdir(parents=True, exist_ok=True)
    original, adapted, mutant = (directory / name for name in ("original.S", "adapted.S", "mutant.S"))
    original.write_bytes((source / relative).read_bytes())
    adapted.write_bytes((overlay / relative).read_bytes())
    (directory / "__set_errno.cpp").write_bytes((source / "libc/bionic/__set_errno.cpp").read_bytes())
    original_object = directory / "original.o"
    run("clang", *assembly_flags, *includes, "-c", original, "-o", original_object)
    control = inspect(original_object)
    if control["svc"] != 1 or control["tpidr_el0_read"] != 1:
        raise RuntimeError("The original vfork control no longer tests both native boundaries")

    # Remove only the save/restore of the two caller-saved live values. Keep the
    # valid call frame so the negative control fails safely through owned data.
    text = adapted.read_text(encoding="utf-8")
    for line in ("    stp     x9, x10, [sp]\n", "    ldp     x9, x10, [sp]\n"):
        if text.count(line) != 1:
            raise RuntimeError("The vfork preservation mutation no longer has a unique target")
        text = text.replace(line, "", 1)
    mutant.write_text(text, encoding="utf-8", newline="\n")
    mutant_object = directory / "mutant.o"
    run("clang", *assembly_flags, *includes, "-c", mutant, "-o", mutant_object)
    fixture = ROOT / "fixtures/bionic-vfork"
    caller, reply = directory / "check.o", directory / "reply.o"
    run("clang", "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC",
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-mbranch-protection=none",
        "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
        "-c", fixture / "check.c", "-o", caller)
    run("clang", "--target=aarch64-linux-android35", "-fPIC", "-march=armv8-a",
        "-c", fixture / "reply.S", "-o", reply)
    report = {"cases": 28, "mutation_result": -1000,
              "upstream_sha256": digest(original), "adapted_sha256": digest(adapted),
              "mutant_source_sha256": digest(mutant), "upstream_inventory": control,
              "production_objects": {name: digest(objects[name]) for name in
                                     (relative, "libc/bionic/__set_errno.cpp")},
              "fixture_sources": {str(path.relative_to(ROOT)).replace(os.sep, "/"): digest(path)
                                  for path in (fixture / "check.c", fixture / "reply.S", fixture / "linux.c")}}
    for name, obj in (("native", objects[relative]), ("mutant", mutant_object)):
        raw, prefixed = directory / (name + "-raw.o"), directory / (name + "-prefixed.o")
        output = directory / (name + "-test.o")
        run("ld.lld", "-r", obj, objects["libc/bionic/__set_errno.cpp"], "-o", raw)
        run("llvm-objcopy", "--prefix-symbols=artbox_vfork_", raw, prefixed)
        run("ld.lld", "-r", prefixed, caller, reply, "-o", output)
        if run("llvm-nm", "--undefined-only", output).strip():
            raise RuntimeError("The vfork oracle has unresolved imports")
        boundary = inspect(output)
        if not boundary["instruction_count"] or any(v for k, v in boundary.items() if k != "instruction_count"):
            raise RuntimeError("The vfork oracle violates the native instruction boundary")
        report[name] = {"object_sha256": digest(output), "inventory": boundary}
    return report
