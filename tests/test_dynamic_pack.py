"""Controlled ELF wrapper layout and final linked-image rejection tests."""
import sys
sys.dont_write_bytecode = True

from pathlib import Path
import struct
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from wrap_dynamic import pack_layout, verify_macho

PAGE = 16384


def fixture():
    data = bytearray(PAGE + 128)
    struct.pack_into("<16sHHIQQQI6H", data, 0, b"\x7fELF\x02\x01\x01" + bytes(9),
                     3, 183, 1, 0, 64, 0, 0, 64, 56, 3, 0, 0, 0)
    struct.pack_into("<II6Q", data, 64, 1, 5, 0, 0, 0, 512, 512, PAGE)
    struct.pack_into("<II6Q", data, 120, 1, 6, PAGE, PAGE, PAGE, 128, 256, PAGE)
    struct.pack_into("<II6Q", data, 176, 2, 6, PAGE, PAGE, PAGE, 16, 16, 8)
    return data


def macho(rx, rw):
    result = bytearray(3 * PAGE)
    commands = bytearray()
    for name, address, size, fileoff, perms, blob in [
            (b"__TEXT", 0, 2 * PAGE, 0, 5, rx),
            (b"__DATA", 2 * PAGE, PAGE, 2 * PAGE, 3, rw)]:
        section_address = PAGE if name == b"__TEXT" else address
        section_offset = PAGE if name == b"__TEXT" else fileoff
        section = struct.pack("<16s16sQQ8I", b"__artbox", name, section_address,
                              len(blob), section_offset, 14, 0, 0, 0, 0, 0, 0)
        commands += struct.pack("<II16s4Q4I", 0x19, 152, name, address, size,
                                fileoff, size, perms, perms, 1, 0) + section
        result[section_offset:section_offset + len(blob)] = blob
    struct.pack_into("<8I", result, 0, 0xfeedfacf, 0x100000c, 0, 6, 2, len(commands), 0, 0)
    result[32:32 + len(commands)] = commands
    return result


class DynamicPack(unittest.TestCase):
    def test_tls_template_is_alias_of_writable_initializer_and_zero_fill(self):
        data = fixture()
        struct.pack_into('<H', data, 56, 4)
        struct.pack_into('<II6Q', data, 232, 7, 4, PAGE+32, PAGE+32, PAGE+32, 8, 64, 32)
        rx, rw, _ = pack_layout(data)
        self.assertEqual(rw[32:40], data[PAGE+32:PAGE+40])
        for position, value in ((248, 0), (272, 512), (264, 120), (236, 5)):
            bad = bytearray(data)
            struct.pack_into('<I' if position == 236 else '<Q', bad, position, value)
            with self.assertRaises(ValueError): pack_layout(bad)

    def test_layout_preserves_virtual_offsets_and_zero_fills_data_bss(self):
        data = fixture()
        rx, rw, layout = pack_layout(data)
        self.assertEqual(rx[:512], data[:512])
        self.assertEqual(rx[512:], bytes(PAGE - 512))
        self.assertEqual(rw[:128], data[PAGE:])
        self.assertEqual(rw[128:], bytes(128))
        linked = macho(rx, rw)
        verified = verify_macho(linked, layout)
        self.assertEqual(verified["load_bias"], PAGE)
        self.assertEqual(verified["rw_address"], 2 * PAGE)

    def test_reject_invalid_elf_mapping_before_pack(self):
        changes = [(16, "H", 2), (18, "H", 62), (68, "I", 7), (124, "I", 7),
                   (80, "Q", PAGE), (136, "Q", 513), (160, "Q", 127),
                   (160, "Q", 1 << 40), (152, "Q", PAGE), (120, "I", 7),
                   (192, "Q", 300), (56, "H", 100)]
        for offset, fmt, value in changes:
            with self.subTest(offset=offset, value=value):
                data = fixture(); struct.pack_into("<" + fmt, data, offset, value)
                with self.assertRaises(ValueError): pack_layout(data)

    def test_reject_linker_layout_drift_permissions_and_changed_payload(self):
        rx, rw, layout = pack_layout(fixture())
        original = macho(rx, rw)
        mutations = [(PAGE + 300, "B", 1), (2 * PAGE + 127, "B", 1),
                     (32 + 56, "I", 7), (32 + 60, "I", 7), (32 + 152 + 60, "I", 5),
                     (32 + 152 + 72 + 32, "Q", 3 * PAGE),
                     (32 + 152 + 72 + 40, "Q", 255), (32 + 4, "I", 4)]
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, value=value):
                data = bytearray(original); struct.pack_into("<" + fmt, data, offset, value)
                with self.assertRaises(ValueError): verify_macho(data, layout)


if __name__ == "__main__":
    unittest.main()
