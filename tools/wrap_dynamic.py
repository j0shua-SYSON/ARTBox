"""Build-time wrapper for a controlled two-segment AArch64 shared ELF.

This preserves one ELF load bias. It neither resolves imports nor makes data
executable. The build must audit final ELF instructions, link with Apple tools,
verify the resulting layout and sign it before runtime use.
"""
import sys
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

PAGE = 16384
LIMIT = 64 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(data, fmt, offset):
    size = struct.calcsize(fmt)
    require(0 <= offset <= len(data) - size, "Truncated binary structure")
    return struct.unpack_from(fmt, data, offset)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pack_layout(data):
    require(len(data) <= LIMIT, "ELF exceeds the controlled wrapper size limit")
    ident, kind, machine, version, entry, phoff, _, flags, ehsize, phsize, phnum, _, _, _ = read(data, "<16sHHIQQQI6H", 0)
    require(ident[:7] == b"\x7fELF\x02\x01\x01" and ident[7] in (0, 3), "Expected little-endian ELF64")
    require((kind, machine, version, flags, ehsize, phsize) == (3, 183, 1, 0, 64, 56), "Expected AArch64 ET_DYN")
    require(phoff >= 64 and 2 <= phnum <= 16, "Invalid program header table")
    loads, dynamic, tls = [], [], []
    allowed = {1, 2, 4, 6, 7, 0x6474e550, 0x6474e551, 0x6474e552}
    for index in range(phnum):
        ptype, perms, offset, address, _, size, memory, alignment = read(data, "<II6Q", phoff + index * 56)
        require(ptype in allowed and perms & ~7 == 0, "Unsupported program header")
        require(offset <= len(data) and size <= len(data) - offset and size <= memory,
                "Program header exceeds file or memory bounds")
        require(address + memory <= 1 << 64, "Virtual address overflow")
        require(alignment in (0, 1) or alignment & (alignment - 1) == 0, "Invalid segment alignment")
        segment = {"index": index, "address": address, "offset": offset, "file_size": size,
                   "memory_size": memory, "flags": perms, "alignment": alignment}
        if ptype == 1:
            loads.append(segment)
        elif ptype == 2:
            dynamic.append(segment)
        elif ptype == 7:
            tls.append(segment)
        elif ptype == 0x6474e551:
            require(perms & 1 == 0, "Executable guest stack is unsupported")
    require(len(loads) == 2 and len(dynamic) == 1, "Wrapper requires exactly RX/RW loads and a dynamic table")
    code, writable = sorted(loads, key=lambda segment: segment["address"])
    require(code["flags"] == 5 and writable["flags"] == 6, "Expected distinct RX and RW segments")
    require(code["address"] == code["offset"] == 0 and code["file_size"] == code["memory_size"],
            "Read-only image must begin at ELF zero and have no executable BSS")
    require(code["file_size"] >= phoff + phnum * 56 and entry < code["file_size"], "ELF header or entry escapes RX image")
    for segment in loads:
        require(segment["alignment"] == PAGE and segment["address"] % PAGE == segment["offset"] % PAGE == 0,
                "Wrapper requires 16 KiB aligned ELF segments")
    split, size = writable["address"], writable["memory_size"]
    require(code["memory_size"] <= split and 0 < size <= LIMIT and split + size <= LIMIT,
            "Overlapping or excessive ELF virtual image")
    require(writable["offset"] >= code["file_size"], "Overlapping ELF file segments")
    require(len(tls) <= 1, "Duplicate TLS template")
    for template in tls:
        relative = template["address"] - split
        require(template["flags"] & 1 == 0 and 0 <= relative <= size and
                0 < template["memory_size"] <= size - relative and
                template["alignment"] <= 1024 * 1024, "TLS template escapes writable load")
        require(not template["file_size"] or
                (relative <= writable["file_size"] and template["file_size"] <= writable["file_size"] - relative and
                 template["offset"] == writable["offset"] + relative), "TLS initializer is not mapped data")
    table = dynamic[0]
    relative = table["address"] - split
    require(table["flags"] == 6 and 0 <= relative <= writable["file_size"] - table["file_size"] and
            16 <= table["file_size"] == table["memory_size"] and table["file_size"] % 16 == 0 and
            table["offset"] == writable["offset"] + relative, "Dynamic table must be file-backed writable data")
    rx = bytes(data[:code["file_size"]]) + bytes(split - code["file_size"])
    rw = bytes(data[writable["offset"]:writable["offset"] + writable["file_size"]]) + bytes(size - writable["file_size"])
    layout = {"scope": "Controlled shared ELF wrapper; imports, relocations and startup require the runtime loader",
              "input_sha256": digest(data), "rx_bytes": len(rx), "rw_bytes": len(rw),
              "rx_sha256": digest(rx), "rw_sha256": digest(rw), "loads": [code, writable]}
    return rx, rw, layout


def verify_macho(data, layout):
    magic, cpu, subtype, kind, count, commands_size, _, _ = read(data, "<8I", 0)
    require((magic, cpu, subtype, kind) == (0xfeedfacf, 0x100000c, 0, 6), "Expected arm64 Mach-O dylib")
    require(count <= 256 and commands_size <= len(data) - 32, "Invalid Mach-O command table")
    position, end, found = 32, 32 + commands_size, {}
    for _ in range(count):
        cmd, size = read(data, "<II", position)
        require(size >= 8 and size % 8 == 0 and position + size <= end, "Invalid Mach-O load command")
        if cmd == 0x19:
            _, _, name, address, memory, offset, file_size, maximum, initial, sections, _ = read(data, "<II16s4Q4I", position)
            name = name.split(b"\0", 1)[0]
            require(size == 72 + sections * 80 and address + memory <= 1 << 64 and
                    offset <= len(data) and file_size <= len(data) - offset and file_size <= memory,
                    "Invalid Mach-O segment bounds")
            require(initial & 6 != 6 and maximum & 6 != 6 and initial & ~maximum == 0,
                    "Invalid or writable/executable Mach-O segment")
            for index in range(sections):
                section = read(data, "<16s16sQQ8I", position + 72 + index * 80)
                sname, segment = (word.split(b"\0", 1)[0] for word in section[:2])
                if sname != b"__artbox":
                    continue
                require(segment == name and name in (b"__TEXT", b"__DATA") and name not in found,
                        "Duplicate or misplaced guest section")
                target, length, fileoff, alignment, relocation_offset, relocations, flags = section[2:9]
                require(address <= target <= address + memory - length and offset <= fileoff <= offset + file_size - length,
                        "Guest section escapes its segment")
                require(target - address == fileoff - offset and target % PAGE == 0 and alignment == 14 and
                        relocation_offset == relocations == 0 and flags & 0xff == 0,
                        "Guest section addressing, alignment or Mach-O relocations changed")
                role = "rx" if name == b"__TEXT" else "rw"
                require(initial == (5 if role == "rx" else 3) and length == layout[role + "_bytes"] and
                        digest(data[fileoff:fileoff + length]) == layout[role + "_sha256"],
                        "Guest bytes, size or permissions changed during Apple linking")
                found[name] = target
        position += size
    require(position == end and len(found) == 2, "Incomplete guest wrapper load commands")
    bias = found[b"__TEXT"]
    require(found[b"__DATA"] == bias + layout["loads"][1]["address"], "Apple linker did not preserve the ELF load bias")
    return {"load_bias": bias, "rx_address": bias, "rw_address": found[b"__DATA"], "macho_sha256": digest(data)}


def main():
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
    from environment import environment
    os.environ.update(environment())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    require(args.elf.stat().st_size <= LIMIT, "ELF exceeds wrapper size limit")
    rx, rw, layout = pack_layout(args.elf.read_bytes())
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "image.rx").write_bytes(rx)
    (args.output / "image.rw").write_bytes(rw)
    (args.output / "layout.json").write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")
    (args.output / "wrapper.S").write_text('''.section __TEXT,__artbox,regular
.p2align 14
.globl _artbox_dynamic_rx
_artbox_dynamic_rx:
.incbin "image.rx"
.section __DATA,__artbox,regular
.p2align 14
.globl _artbox_dynamic_rw
_artbox_dynamic_rw:
.incbin "image.rw"
''', encoding="utf-8", newline="\n")
    print(f"Prepared {len(rx)} RX and {len(rw)} RW bytes; final Apple layout verification and signing required")


if __name__ == "__main__":
    main()
