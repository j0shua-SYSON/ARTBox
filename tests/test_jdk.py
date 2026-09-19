"""Portable JDK extraction preserves tools and rejects archive path escapes."""
import sys
sys.dont_write_bytecode = True
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import environment
from jdk import extract


class PortableJdk(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def archive(self, entries):
        path = self.root / 'jdk.tar.gz'
        with tarfile.open(path, 'w:gz') as archive:
            for name, kind, value in entries:
                member = tarfile.TarInfo(name)
                member.type = kind
                member.mode = 0o755
                if kind == tarfile.REGTYPE:
                    member.size = len(value)
                    archive.addfile(member, io.BytesIO(value))
                else:
                    member.linkname = value
                    archive.addfile(member)
        return path

    def test_tar_tools_and_internal_aliases(self):
        archive = self.archive([('jdk/bin/java', tarfile.REGTYPE, b'executable'),
                                ('jdk/bin/tool', tarfile.SYMTYPE, 'java'),
                                ('jdk/bin/hard', tarfile.LNKTYPE, 'jdk/bin/java')])
        output = self.root / 'output'
        extract(archive, output, 'jdk')
        for name in ('java', 'tool', 'hard'):
            path = output / 'bin' / name
            self.assertEqual(path.read_bytes(), b'executable')
            self.assertFalse(path.is_symlink())
            if os.name != 'nt':
                self.assertTrue(path.stat().st_mode & 0o111)

    def test_zip_tools_preserve_arbitrary_binary_bytes(self):
        archive = self.root / 'jdk.zip'
        data = b'\x00\xff\xfe\x80native-code'
        with zipfile.ZipFile(archive, 'w') as bundle:
            bundle.writestr('jdk/bin/java.exe', data)
        output = self.root / 'zip-output'
        extract(archive, output, 'jdk')
        self.assertEqual((output / 'bin/java.exe').read_bytes(), data)
        with self.assertRaisesRegex(RuntimeError, 'empty directory'):
            extract(archive, output, 'jdk')

    def test_paths_aliases_and_duplicates_cannot_escape(self):
        cases = [
            [('jdk/../outside', tarfile.REGTYPE, b'data')],
            [('jdk/bin/java', tarfile.SYMTYPE, '../../../outside')],
            [('jdk/bin/java', tarfile.SYMTYPE, '/outside')],
            [('jdk/a', tarfile.SYMTYPE, 'b'), ('jdk/b', tarfile.SYMTYPE, 'a')],
            [('jdk/a', tarfile.REGTYPE, b'a'), ('jdk/A', tarfile.REGTYPE, b'b')],
            [('jdk/hard', tarfile.LNKTYPE, 'other/file')],
        ]
        for index, entries in enumerate(cases):
            with self.subTest(index=index):
                with self.assertRaises(RuntimeError):
                    extract(self.archive(entries), self.root / str(index), 'jdk')
        self.assertFalse((self.root / 'outside').exists())


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
