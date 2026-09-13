"""Use only the reviewed Android compiler-rt arithmetic members from NDK r28c."""
import hashlib
import json
import os
import subprocess

from environment import ROOT
from ndk import REVISION
from bionic_adapt import inventory


def prepare(tools, build):
    pin = json.loads((ROOT / "third_party/bionic/builtins.json").read_text(encoding="utf-8"))
    if pin["ndk_revision"] != REVISION:
        raise RuntimeError("Compiler runtime differs from the selected NDK")
    suffix = ".exe" if os.name == "nt" else ""
    archive = tools.parent / pin["archive"]
    outputs = []
    for member, digest in pin["members"].items():
        if member not in ("comparetf2.c.o", "multf3.c.o"):
            raise RuntimeError("Unreviewed compiler runtime member")
        output = build / member
        with output.open("wb") as stream:
            subprocess.run([str(tools / f"llvm-ar{suffix}"), "p", str(archive), member], stdout=stream, check=True)
        if hashlib.sha256(output.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"NDK runtime member differs from its reviewed hash: {member}")
        outputs.append(output)
    notice = (tools.parent / pin["notice"]).read_bytes()
    if hashlib.sha256(notice).hexdigest() != pin["notice_sha256"]:
        raise RuntimeError("Compiler runtime notice differs from the reviewed NDK")
    (build / "COMPILER-RT-NOTICE.txt").write_bytes(notice)

    caller = build / "binary128-check.o"
    source = ROOT / "fixtures/bionic-binary128/check.c"
    subprocess.run([str(tools / f"clang{suffix}"), "--target=aarch64-linux-android35", "-std=c11", "-O2", "-fPIC",
                    "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-mbranch-protection=none",
                    "-ffixed-x18", "-ffixed-x27", "-ffixed-x28", "-Wall", "-Wextra", "-Werror",
                    "-c", str(source), "-o", str(caller)], check=True)
    test = build / "binary128-test.o"
    subprocess.run([str(tools / f"ld.lld{suffix}"), "-r", *map(str, outputs), str(caller), "-o", str(test)], check=True)
    subprocess.run([str(tools / f"llvm-objcopy{suffix}"), "--prefix-symbols=artbox_quad_", str(test)], check=True)
    if subprocess.check_output([str(tools / f"llvm-nm{suffix}"), "--undefined-only", str(test)]).strip():
        raise RuntimeError("Compiler runtime test has unresolved imports")
    disassembly = subprocess.check_output([str(tools / f"llvm-objdump{suffix}"), "-d", "--no-show-raw-insn", str(test)], text=True)
    boundary = inventory(disassembly)
    if any(boundary[key] for key in ("svc", "tpidr_mentions", "x18_mentions", "x27_mentions", "x28_mentions", "unknown_instructions")):
        raise RuntimeError("Compiler runtime arithmetic violates the native instruction boundary")
    return outputs, {"pin": pin, "inventory": boundary, "cases": 123,
                     "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                     "object_sha256": hashlib.sha256(test.read_bytes()).hexdigest()}
