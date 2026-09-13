"""Check pinned source extraction without downloads or symlink privileges."""
import sys

sys.dont_write_bytecode = True

import hashlib
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from environment import environment
from sources import unpack, verify_archive


class Sources(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / "source.tar.gz"
        self.output = self.root / "output"

    def bundle(self, records):
        with tarfile.open(self.archive, "w:gz") as target:
            for path, kind, value in records:
                item = tarfile.TarInfo(path)
                item.type = kind
                item.mode = 0o755 if kind == tarfile.DIRTYPE else 0o644
                if kind == tarfile.REGTYPE:
                    item.size = len(value)
                    target.addfile(item, io.BytesIO(value))
                else:
                    item.linkname = value
                    target.addfile(item)

    def test_regular_and_internal_header_alias(self):
        self.bundle([("src/include/header.h", tarfile.REGTYPE, b"header\n"),
                     ("src/alias/header.h", tarfile.SYMTYPE, "../include/header.h"),
                     ("src/.clang-format", tarfile.SYMTYPE, "../unfetched/config")])
        unpack(self.archive, self.output, [".clang-format"])
        self.assertEqual((self.output / "alias/header.h").read_bytes(), b"header\n")
        self.assertFalse((self.output / "alias/header.h").is_symlink())
        self.assertFalse((self.output / ".clang-format").exists())

    def test_archive_hash_and_size(self):
        self.archive.write_bytes(b"source")
        spec = {"archive_bytes": 6, "archive_sha256": hashlib.sha256(b"source").hexdigest()}
        verify_archive(self.archive, spec)
        self.archive.write_bytes(b"other!")
        with self.assertRaises(RuntimeError):
            verify_archive(self.archive, spec)
        self.archive.write_bytes(b"source-more")
        with self.assertRaises(RuntimeError):
            verify_archive(self.archive, spec)

    def test_reject_unsafe_members_before_writing(self):
        bad = [
            ("src/../escape", tarfile.REGTYPE, b"bad"),
            ("/absolute", tarfile.REGTYPE, b"bad"),
            ("src/drive:escape", tarfile.REGTYPE, b"bad"),
            ("src/back\\slash", tarfile.REGTYPE, b"bad"),
            ("second-root/file", tarfile.REGTYPE, b"bad"),
            ("src/link", tarfile.SYMTYPE, "../../escape"),
            ("src/link", tarfile.SYMTYPE, "/absolute"),
            ("src/link", tarfile.SYMTYPE, "link"),
            ("src/link", tarfile.LNKTYPE, "src/good"),
            ("src/device", tarfile.CHRTYPE, ""),
            ("src/good", tarfile.REGTYPE, b"duplicate"),
            ("src/GOOD", tarfile.REGTYPE, b"case collision"),
            ("src/good/child", tarfile.REGTYPE, b"file as parent"),
        ]
        for record in bad:
            with self.subTest(record=record):
                self.bundle([("src/good", tarfile.REGTYPE, b"good"), record])
                with self.assertRaises(RuntimeError):
                    unpack(self.archive, self.output, [])
                self.assertFalse((self.output / "good").exists())
                self.assertFalse((self.root / "escape").exists())


if __name__ == "__main__":
    os.environ.update(environment())
    unittest.main()
