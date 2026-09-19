"""Reject changed annotation declarations and damaged DEX build output."""
import sys
sys.dont_write_bytecode = True
import hashlib
from pathlib import Path
import struct
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_art_classlib import annotation_keys, dex_envelope
from dex_fixture import make_hello


class ClassLibraryBuild(unittest.TestCase):
    def test_annotation_declarations_require_both_hash_and_names(self):
        data = b'package: "com.example"\nflag {\n  name: "enabled"\n}\n'
        config = {'icu_aconfig_sha256': hashlib.sha256(data).hexdigest(),
                  'icu_annotation_keys': {'enabled': 'com.example.enabled'}}
        self.assertIn('FLAG_ENABLED = "com.example.enabled"', annotation_keys(data, config))
        changed = data.replace(b'enabled', b'renamed')
        with self.assertRaisesRegex(RuntimeError, 'reviewed input'):
            annotation_keys(changed, config)
        config['icu_aconfig_sha256'] = hashlib.sha256(changed).hexdigest()
        with self.assertRaisesRegex(RuntimeError, 'reviewed declarations'):
            annotation_keys(changed, config)

    def test_dex_envelope_checks_integrity_and_version(self):
        original, _ = make_hello()
        data = bytearray(original)
        data[4:7] = b'039'
        data[12:32] = hashlib.sha1(data[32:]).digest()
        struct.pack_into('<I', data, 8, zlib.adler32(data[12:]))
        result = dex_envelope('classes.dex', data)
        self.assertEqual(result['classes'], 1)
        self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())
        for offset in (0, 8, 12, 32, 36, 40, len(data)-1):
            with self.subTest(offset=offset):
                changed = bytearray(data)
                changed[offset] ^= 1
                with self.assertRaisesRegex(RuntimeError, 'Invalid DEX envelope'):
                    dex_envelope('damaged.dex', changed)


if __name__ == '__main__':
    unittest.main()
