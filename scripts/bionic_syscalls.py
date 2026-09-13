"""Use the pinned AOSP generator, then replace ARM64 kernel entry before signing."""
import contextlib
import hashlib
import importlib.util
import io
import json
import re

from environment import ROOT


def call_frame(moves=""):
    return ("    // ARTBox: fixed-width C call; preserve the caller's frame and return address.\n"
            "    stp     x29, x30, [sp, #-16]!\n"
            "    .cfi_def_cfa_offset 16\n    .cfi_offset x29, -16\n    .cfi_offset x30, -8\n"
            "    mov     x29, sp\n" + moves +
            "    bl      artbox_bionic_syscall\n"
            "    ldp     x29, x30, [sp], #16\n"
            "    .cfi_def_cfa_offset 0\n    .cfi_restore x29\n    .cfi_restore x30\n")


def adapt_generated(source, calls):
    seen = set()
    for call in calls:
        function, number, count = call["func"], call["name"], len(call["params"])
        if function in seen or not re.fullmatch(r"[A-Za-z_]\w*", function) or \
                not re.fullmatch(r"[A-Za-z_]\w*", number) or count > 6:
            raise RuntimeError("Unsupported or duplicate ARM64 syscall declaration")
        seen.add(function)
        before = f"ENTRY({function})\n    mov     x8, __NR_{number}\n    svc     #0\n"
        if source.count(before) != 1:
            raise RuntimeError(f"AOSP syscall template drift or duplicate entry: {function}")
        moves = "".join(f"    mov     x{i + 1}, x{i}\n" for i in reversed(range(count)))
        moves += "".join(f"    mov     x{i}, xzr\n" for i in range(count + 1, 7))
        moves += f"    mov     x0, __NR_{number}\n"
        source = source.replace(before, f"ENTRY({function})\n" + call_frame(moves), 1)
    if re.search(r"\bsvc\s", source):
        raise RuntimeError("Unadapted kernel entry remains in generated Bionic syscalls")
    return source


def generate(source, directory, profile):
    pin = json.loads((ROOT / "third_party/bionic/syscalls.json").read_text(encoding="utf-8"))
    for relative, digest in pin["inputs"].items():
        if hashlib.sha256((source / relative).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"Bionic syscall source differs from the reviewed pin: {relative}")
    spec = importlib.util.spec_from_file_location("artbox_aosp_gensyscalls", source / "libc/tools/gensyscalls.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parser = module.SysCallsTxtParser()
    parser.parse_file(source / "libc/SYSCALLS.TXT")
    calls = [call for call in parser.syscalls if call.get("arm64")]
    stream = io.StringIO()
    with contextlib.redirect_stdout(stream):
        module.main("arm64", source / "libc/SYSCALLS.TXT")
    upstream = stream.getvalue()
    assembly = adapt_generated(upstream, calls) if profile == "native" else upstream
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "syscalls-arm64.S"
    path.write_text(assembly, encoding="utf-8", newline="\n")
    # Only the test copy gets prefixed symbols; these are never guest exports.
    header = ["// Generated test declarations from pinned AOSP SYSCALLS.TXT.", "#include <asm/unistd.h>"]
    entries = []
    for call in calls:
        for name in [call["func"], *call["aliases"]]:
            header.append(f'extern "C" void artbox_stub_{name}();')
            entries.append(f'    {{artbox_stub_{name}, __NR_{call["name"]}, {len(call["params"])}, "{name}"}},')
    header += ['extern "C" void artbox_stub_syscall();',
               'struct Stub { void (*function)(); uint64_t number; unsigned arguments; const char* name; };',
               'static const Stub stubs[] = {', *entries, '};']
    test_header = directory / "syscall-test.h"
    test_header.write_text("\n".join(header) + "\n", encoding="utf-8", newline="\n")
    return path, {"pin": pin, "stubs": len(calls), "aliases": sum(len(c["aliases"]) for c in calls),
                  "upstream_generated_sha256": hashlib.sha256(upstream.encode()).hexdigest(),
                  "generated_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                  "test_header_sha256": hashlib.sha256(test_header.read_bytes()).hexdigest()}
