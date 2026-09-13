"""Packaging contract for a supplied build tool, ELF fixture and output directory."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

tool, fixture, output = map(Path, sys.argv[1:4])
pack_command = [sys.executable, "-B", str(tool)] if tool.suffix == ".py" else [str(tool)]
output = output.resolve()
output.mkdir(parents=True, exist_ok=True)
original = fixture.read_bytes()
digest = hashlib.sha256(original).hexdigest()
subprocess.run([*pack_command, str(fixture), str(output), "macos"], check=True)
info = json.loads((output / "pack-info.json").read_text())
payload = (output / "payload.bin").read_bytes()
assert info["svc_sites"] == 6 and info["entry_offset"] == 0
assert len(payload) == info["payload_bytes"]
text_offset = struct.unpack_from("<Q", original, 72)[0]
for offset in range(0, info["text_bytes"], 4):
    before = struct.unpack_from("<I", original, text_offset + offset)[0]
    after = struct.unpack_from("<I", payload, offset)[0]
    if before == 0xD4000001:
        assert after & 0xFC000000 == 0x14000000
        signed = ((after & 0x3FFFFFF) ^ 0x2000000) - 0x2000000
        target = offset + signed * 4
        assert target >= info["original_segment_bytes"] and target + 128 <= len(payload)
    else:
        assert before == after
converted = (output / "converted.dylib").read_bytes()
magic, cpu, subtype, kind, commands, command_bytes, flags, reserved = struct.unpack_from("<8I", converted)
assert (magic, cpu, kind) == (0xFEEDFACF, 0x100000C, 6)
assert converted[16384:16384 + len(payload)] == payload
cursor = 32
segments = []
for _ in range(commands):
    command, size = struct.unpack_from("<II", converted, cursor)
    assert size >= 8 and cursor + size <= 32 + command_bytes
    if command == 0x19:
        address, vsize, fileoff, filesize, maximum, initial = struct.unpack_from("<4Q2I", converted, cursor + 24)
        assert address % 16384 == 0 and fileoff % 16384 == 0
        assert initial & 6 != 6 and maximum & 6 != 6
        assert fileoff + filesize <= len(converted)
        segments.append((address, vsize))
    cursor += size
assert cursor == 32 + command_bytes and len(segments) == 2

mutations = []
bad = bytearray(original); bad[0] = 0; mutations.append(("magic", bad))
bad = bytearray(original); struct.pack_into("<I", bad, 68, 7); mutations.append(("rwx", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0xD2800012); mutations.append(("reserved-x18", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0xD280001B); mutations.append(("reserved-x27", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0xD280001C); mutations.append(("reserved-x28", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0xD65F03C0); mutations.append(("ret", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0xD53BD040); mutations.append(("tls", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0x14010000); mutations.append(("branch-outside", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset, 0x4EA01C00); mutations.append(("simd", bad))
mutations.append(("truncated-header", original[:63]))
bad = bytearray(original); struct.pack_into("<Q", bad, 40, len(original) - 1); mutations.append(("section-table", bad))
bad = bytearray(original); struct.pack_into("<Q", bad, 96, len(original)); mutations.append(("segment-size", bad))
bad = bytearray(original); struct.pack_into("<Q", bad, 24, 0); mutations.append(("entry", bad))
bad = bytearray(original); struct.pack_into("<I", bad, text_offset + info["text_bytes"] - 4, 0xD503201F); mutations.append(("fallthrough", bad))
section_table = struct.unpack_from("<Q", original, 40)[0]
bad = bytearray(original); struct.pack_into("<Q", bad, section_table + 64 + 8, 0); mutations.append(("text-not-allocated", bad))
for name, data in mutations:
    path = output / (name + ".elf")
    path.write_bytes(data)
    rejected = output / name
    rejected.mkdir(exist_ok=True)
    run = subprocess.run([*pack_command, str(path), str(rejected), "macos"], capture_output=True, timeout=10)
    assert run.returncode != 0 and run.stderr, name
    assert not list(rejected.iterdir()), name
assert hashlib.sha256(fixture.read_bytes()).hexdigest() == digest
print(f"packaging: retained text, six static SVC veneers, Mach-O layout and {len(mutations)} rejection cases PASS")
