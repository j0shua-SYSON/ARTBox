"""Keep host adaptations separate, reproducible and fail closed on source drift."""
import sys
sys.dont_write_bytecode = True
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import environment
from art_host_adapt import adapt_sources


class HostSources(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / 'source'
        self.output = Path(self.temp.name) / 'build'
        self.source.mkdir()
        self.original = b'/* upstream license */\nold\n'
        (self.source / 'unit.cc').write_bytes(self.original)
        (self.source / 'sibling.h').write_bytes(b'original sibling\n')
        self.selection = [{'path': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                          for p in sorted(self.source.iterdir())]
        self.patch = {'files': [{'path': 'unit.cc', 'sha256': hashlib.sha256(self.original).hexdigest(),
                                'replacements': [{'before': 'old\n', 'after': 'adapted\n'}]}]}

    def test_originals_and_sibling_includes_are_preserved(self):
        records = adapt_sources(self.source, self.output, self.selection, self.patch)
        self.assertEqual((self.source / 'unit.cc').read_bytes(), self.original)
        self.assertEqual((self.output / 'unit.cc').read_bytes(), b'/* upstream license */\nadapted\n')
        self.assertEqual((self.output / 'sibling.h').read_bytes(), b'original sibling\n')
        self.assertEqual(records[0]['adapted_sha256'], hashlib.sha256((self.output / 'unit.cc').read_bytes()).hexdigest())

    def test_every_input_is_validated_before_writing(self):
        (self.source / 'sibling.h').write_bytes(b'changed\n')
        with self.assertRaises(RuntimeError):
            adapt_sources(self.source, self.output, self.selection, self.patch)
        self.assertFalse(self.output.exists())
        (self.source / 'sibling.h').write_bytes(b'original sibling\n')
        self.patch['files'][0]['replacements'][0]['before'] = 'missing'
        with self.assertRaises(RuntimeError):
            adapt_sources(self.source, self.output, self.selection, self.patch)
        self.assertFalse(self.output.exists())

    def test_rejects_overlap_and_unselected_or_unsafe_paths(self):
        with self.assertRaises(RuntimeError):
            adapt_sources(self.source, self.source / 'output', self.selection, self.patch)
        for name in ('../escape', '/escape', 'C:/escape', 'x\\escape', 'unselected.cc'):
            with self.subTest(name=name):
                self.patch['files'][0]['path'] = name
                with self.assertRaises(RuntimeError):
                    adapt_sources(self.source, self.output, self.selection, self.patch)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
