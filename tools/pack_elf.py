"""Package the controlled M1 ARM64 ELF as two unsigned, build-time Mach-O inputs.

This is a host tool, never a runtime loader. Apple tools must link/sign the
outputs before execution. The initial instruction contract is intentionally
small; rejection is preferable to silently accepting an unaudited ABI.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

PAGE = 16384
LIMIT = 2 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def align(value, alignment):
    return (value + alignment - 1) & -alignment


def signed(value, bits):
    return (value ^ (1 << (bits - 1))) - (1 << (bits - 1))


def parse_elf(data):
    def read(fmt, offset):
        size = struct.calcsize(fmt)
        require(0 <= offset <= len(data) - size, "truncated ELF structure")
        return struct.unpack_from(fmt, data, offset)

    ident, kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, names_index = read("<16sHHIQQQI6H", 0)
    require(ident[:7] == b"\x7fELF\x02\x01\x01" and ident[7] in (0, 3), "expected little-endian ELF64")
    require((kind, machine, version, flags, ehsize) == (2, 183, 1, 0, 64), "expected static AArch64 ET_EXEC")
    require(phsize == 56 and phnum == 1 and phoff >= 64, "M1 requires one static image segment")
    ptype, perms, offset, base, physical, size, memory, alignment = read("<II6Q", phoff)
    require(ptype == 1 and perms == 5, "M1 requires one read/execute PT_LOAD")
    require(0 < size == memory <= LIMIT // 2 and offset <= len(data) - size, "invalid segment bounds or BSS")
    require(base == entry and base + size <= 1 << 64 and physical == base, "unsupported entry or address layout")
    require(alignment == PAGE and base % PAGE == offset % PAGE == 0, "expected 16 KiB aligned image")
    require(shsize == 64 and 0 < shnum <= 64 and 0 < names_index < shnum and shoff >= 64, "invalid section table")
    sections = [read("<II4QII2Q", shoff + index * 64) for index in range(shnum)]
    names = sections[names_index]
    require(names[1] == 3 and names[4] <= len(data) - names[5], "invalid section names")
    strings = data[names[4]:names[4] + names[5]]
    by_name = {}
    for index, section in enumerate(sections):
        nameoff, stype, sflags, address, fileoff, length, link, target, salign, entsize = section
        require(nameoff < len(strings), "invalid section name offset")
        end = strings.find(b"\0", nameoff)
        require(end >= 0, "unterminated section name")
        name = strings[nameoff:end].decode("ascii")
        require(name not in by_name, "duplicate section name")
        require(stype in (0, 1, 2, 3, 4), "unsupported section type")
        require(fileoff <= len(data) - length, "section outside file")
        if sflags & 2:
            require(name in (".text", ".rodata") and stype == 1, "unsupported allocated section")
            require(sflags == (6 if name == ".text" else 2), "unexpected allocated permissions")
            require(base <= address <= base + size - length and fileoff - offset == address - base,
                    "allocated section does not preserve image addressing")
        require(salign == 0 or salign & (salign - 1) == 0, "invalid section alignment")
        by_name[name] = (index, section)
    require(".text" in by_name and ".rodata" in by_name, "missing text or constants")
    text_index, text = by_name[".text"]
    rodata = by_name[".rodata"][1]
    require(text[2] == 6 and rodata[2] == 2, "expected allocated text and read-only constants")
    require(text[3] == entry and text[4] == offset and text[5] % 4 == 0 and text[5] > 0,
            "unsupported text/entry layout")
    require(rodata[3] >= text[3] + text[5] and rodata[3] + rodata[5] == base + size,
            "unsupported constant layout")
    require(".rela.text" in by_name, "retain relocations with --emit-relocs")
    for name, (_, section) in by_name.items():
        if section[1] != 4:
            continue
        require(name == ".rela.text" and section[7] == text_index and section[9] == 24 and section[5] % 24 == 0,
                "unsupported relocation table")
        require(section[6] < shnum, "invalid relocation symbol table")
        symbols = sections[section[6]]
        require(symbols[1] == 2 and symbols[9] == 24 and symbols[5] % 24 == 0, "invalid symbol table")
        for pos in range(section[4], section[4] + section[5], 24):
            address, info, addend = read("<QQq", pos)
            require(info & 0xffffffff == 274 and info >> 32 < symbols[5] // 24,
                    "only resolved R_AARCH64_ADR_PREL_LO21 is supported")
            require(entry <= address <= entry + text[5] - 4 and address % 4 == 0, "invalid relocation target")
            symbol = read("<IBBHQQ", symbols[4] + (info >> 32) * 24)
            require(0 < symbol[3] < shnum, "undefined relocation symbol")
            target = symbol[4] + addend
            require(base <= target < base + size, "relocation escapes the image")
            word = read("<I", offset + address - base)[0]
            displacement = signed(((word >> 5 & 0x7ffff) << 2) | (word >> 29 & 3), 21)
            require(word & 0x9f000000 == 0x10000000 and address + displacement == target,
                    "unresolved or mismatched ADR relocation")
    return data[offset:offset + size], text[5]


def inspect_text(image, text_size):
    require(struct.unpack_from("<I", image, text_size - 4)[0] & 0xfc000000 == 0x14000000,
            "text must end in an unconditional branch, with no fallthrough into constants")
    sites = []
    for pc in range(0, text_size, 4):
        word = struct.unpack_from("<I", image, pc)[0]
        registers = []
        target = None
        if word == 0xd4000001:  # SVC #0; replaced only in the build output.
            sites.append(pc)
        elif word == 0xd503201f:  # NOP
            pass
        elif word & 0xff800000 in (0xd2800000, 0x92800000):  # MOVZ/MOVN Xd
            registers = [word & 31]
        elif word & 0xffe0ffe0 == 0xaa0003e0:  # MOV Xd, Xm
            registers = [word & 31, word >> 16 & 31]
        elif word & 0xffc0001f == 0xf100001f:  # CMP Xn, immediate
            registers = [word >> 5 & 31]
        elif word & 0xffe0fc1f == 0xeb00001f:  # CMP Xn, Xm
            registers = [word >> 5 & 31, word >> 16 & 31]
        elif word & 0x9f000000 == 0x10000000:  # ADR Xd, image-relative constant
            registers = [word & 31]
            immediate = ((word >> 5 & 0x7ffff) << 2) | (word >> 29 & 3)
            require(0 <= pc + signed(immediate, 21) < len(image), "ADR escapes image")
        elif word & 0xfc000000 == 0x14000000:  # B (not BL)
            target = pc + signed(word & 0x3ffffff, 26) * 4
        elif word & 0xff000010 == 0x54000000:  # B.cond
            target = pc + signed(word >> 5 & 0x7ffff, 19) * 4
        elif word & 0x7e000000 == 0x34000000:  # CBZ/CBNZ
            registers = [word & 31]
            target = pc + signed(word >> 5 & 0x7ffff, 19) * 4
        elif word & 0xffc00000 in (0xf9400000, 0xf9000000):  # LDR/STR Xt, [Xn, imm]
            registers = [word & 31, word >> 5 & 31]
        else:
            raise ValueError(f"unsupported instruction 0x{word:08x} at +0x{pc:x}")
        require(not {18, 27, 28}.intersection(registers), "input uses a reserved host/bridge register")
        if target is not None:
            require(0 <= target < text_size, "branch escapes text")
    require(sites, "input has no Linux SVC sites")
    return sites


def branch(source, target):
    distance = target - source
    require(distance % 4 == 0 and -(1 << 27) <= distance < 1 << 27, "veneer out of branch range")
    return 0x14000000 | (distance // 4 & 0x3ffffff)


def veneer(address, resume):
    # Save Linux caller-visible GPRs and NZCV. Host C preserves x19-x28.
    # The supported input excludes SIMD, x18, x27, x28 and indirect returns.
    pairs = [(reg, reg + 1) for reg in range(1, 17, 2)] + [(17, 30)]
    words = []
    for index, (first, second) in enumerate(pairs):
        base = 0xa9800000 if index == 0 else 0xa9000000  # STP pre / offset
        immediate = (-160 // 8 & 127) if index == 0 else index * 2
        words.append(base | immediate << 15 | second << 10 | 31 << 5 | first)
    words += [0xd53b4209, 0xf9004be9]  # MRS x9,NZCV; STR x9,[sp,#144]
    for destination, source in ((7, 5), (6, 4), (5, 3), (4, 2), (3, 1), (2, 0), (1, 8), (0, 27)):
        words.append(0xaa0003e0 | source << 16 | destination)
    words += [0xd63f0380, 0xf9404be9, 0xd51b4209]  # BLR x28; LDR x9; MSR NZCV,x9
    for index in range(1, len(pairs)):
        first, second = pairs[index]
        words.append(0xa9400000 | index * 2 << 15 | second << 10 | 31 << 5 | first)
    words.append(0xa8c00000 | 20 << 15 | 2 << 10 | 31 << 5 | 1)  # LDP x1,x2,[sp],#160
    words.append(branch(address + len(words) * 4, resume))
    require(len(words) == 32, "internal veneer frame size mismatch")
    return struct.pack("<32I", *words)


def transformed(image, text_size):
    sites = inspect_text(image, text_size)
    payload = bytearray(image)
    payload += b"\0" * (align(len(payload), 4) - len(payload))
    for site in sites:
        address = len(payload)
        payload += veneer(address, site + 4)
        struct.pack_into("<I", payload, site, branch(site, address))
    return bytes(payload), sites


def uleb(value):
    encoded = bytearray()
    while value >= 128:
        encoded.append((value & 127) | 128)
        value >>= 7
    return bytes(encoded + bytes([value]))


def macho(payload, platform):
    """Minimal MH_DYLIB with an export trie, no rebases/imports, and signing room."""
    text_size = align(PAGE + len(payload), PAGE)
    symbol = b"_artbox_guest_start\0"
    export = b"\0\x01" + symbol + uleb(3 + len(symbol))
    terminal = b"\0" + uleb(PAGE)
    export += uleb(len(terminal)) + terminal + b"\0"
    symoff = text_size + align(len(export), 8)
    stroff = symoff + 16
    strings = b"\0" + symbol
    section = struct.pack("<16s16sQQ8I", b"__artbox", b"__TEXT", PAGE, len(payload), PAGE,
                          14, 0, 0, 0x400, 0, 0, 0)  # S_ATTR_SOME_INSTRUCTIONS
    text = struct.pack("<II16s4Q4I", 0x19, 152, b"__TEXT", 0, text_size, 0, text_size, 5, 5, 1, 0) + section
    linkedit = struct.pack("<II16s4Q4I", 0x19, 72, b"__LINKEDIT", text_size, PAGE, text_size, PAGE, 1, 1, 0, 0)
    install_name = b"@rpath/ARTBoxConverted.framework/ARTBoxConverted\0"
    id_size = align(24 + len(install_name), 8)
    identity = struct.pack("<6I", 0xd, id_size, 24, 0, 0x10000, 0x10000) + install_name
    identity += b"\0" * (id_size - len(identity))
    target, minimum = (2, 15 << 16) if platform == "ios" else (1, 11 << 16)
    build = struct.pack("<6I", 0x32, 24, target, minimum, minimum, 0)
    dyld = struct.pack("<12I", 0x80000022, 48, 0, 0, 0, 0, 0, 0, 0, 0, text_size, len(export))
    symtab = struct.pack("<6I", 2, 24, symoff, 1, stroff, len(strings))
    dysymtab = struct.pack("<20I", 0xb, 80, 0, 0, 0, 1, 1, 0, *([0] * 12))
    commands = text + linkedit + identity + build + dyld + symtab + dysymtab
    header = struct.pack("<8I", 0xfeedfacf, 0x100000c, 0, 6, 7, len(commands), 0x100085, 0)
    require(len(header) + len(commands) < PAGE, "insufficient signing/header space")
    result = bytearray(text_size + PAGE)
    result[:len(header + commands)] = header + commands
    result[PAGE:PAGE + len(payload)] = payload
    result[text_size:text_size + len(export)] = export
    result[symoff:symoff + 16] = struct.pack("<IBBHQ", 1, 0xf, 1, 0, PAGE)
    result[stroff:stroff + len(strings)] = strings
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("platform", choices=("macos", "ios"))
    args = parser.parse_args()
    start = time.perf_counter_ns()
    require(args.elf.stat().st_size <= LIMIT, "input exceeds the M1 file-size limit")
    data = args.elf.read_bytes()
    original, text_size = parse_elf(data)
    payload, sites = transformed(original, text_size)
    converted = macho(payload, args.platform)
    wrapper = '''.section __TEXT,__artbox,regular
.p2align 14
.globl _artbox_guest_start
_artbox_guest_start:
    .incbin "payload.bin"
'''
    info = {
        "input_sha256": hashlib.sha256(data).hexdigest(), "platform": args.platform,
        "original_segment_bytes": len(original), "text_bytes": text_size,
        "payload_bytes": len(payload), "svc_sites": len(sites), "svc_offsets": sites,
        "entry_offset": 0, "converted_unsigned_bytes": len(converted),
        "packaging_ns": time.perf_counter_ns() - start,
        "signature": "unsigned build input; Apple signing is required before execution",
    }
    # Validate all input and construct both outputs before touching the destination.
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "payload.bin").write_bytes(payload)
    (args.output / "converted.dylib").write_bytes(converted)
    (args.output / "wrapper.S").write_bytes(wrapper.encode("utf-8"))
    (args.output / "guest_image.h").write_text(
        f"#define ARTBOX_GUEST_IMAGE_SIZE {len(payload)}u\n", encoding="ascii")
    (args.output / "pack-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(f"Packaged {len(original)} image bytes and {len(sites)} SVC veneers for {args.platform}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, struct.error) as error:
        print(f"ARTBox packaging: {error}", file=sys.stderr)
        sys.exit(1)
