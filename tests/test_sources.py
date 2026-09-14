"""Check pinned source extraction without downloads or symlink privileges."""
import sys

sys.dont_write_bytecode = True

import hashlib
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from environment import environment
from sources import obtain_files, unpack, verify_archive


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

    def file_spec(self):
        contents = {"include/header.h": b"/* original header notice */\n", "NOTICE": b"complete license\n"}
        spec = {"repository": "example/source", "tag": "test-v1", "commit": "a" * 40,
                "notice": "NOTICE", "notice_sha256": hashlib.sha256(contents["NOTICE"]).hexdigest(),
                "files": [{"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                          for name, data in contents.items()]}
        return spec, contents

    def test_exact_files_keep_notices_and_revalidate_cached_bytes(self):
        spec, contents = self.file_spec()
        def download(command, stdout, check):
            name = command[2].split("/contents/", 1)[1].split("?ref=", 1)[0]
            self.assertIn("?ref=" + spec["commit"], command[2])
            stdout.write(contents[name])
        with patch("sources.subprocess.run", side_effect=download) as fetch:
            result = obtain_files("fixture", spec, self.root)
            self.assertEqual(fetch.call_count, 2)
            for name, data in contents.items():
                self.assertEqual((result / name).read_bytes(), data)
            self.assertEqual(json.loads((result / ".artbox-source.json").read_text()), spec)
            self.assertEqual(obtain_files("fixture", spec, self.root), result)
            self.assertEqual(fetch.call_count, 2)
            header = result / "include/header.h"
            header.write_bytes(b"x" * len(contents["include/header.h"]))
            with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                obtain_files("fixture", spec, self.root)

    def test_failed_file_download_is_never_installed(self):
        spec, contents = self.file_spec()
        for corrupt in (b"short", b"x" * len(contents["NOTICE"])):
            def download(command, stdout, check):
                name = command[2].split("/contents/", 1)[1].split("?ref=", 1)[0]
                stdout.write(corrupt if name == "NOTICE" else contents[name])
            with self.subTest(corrupt=corrupt), patch("sources.subprocess.run", side_effect=download):
                with self.assertRaises(RuntimeError):
                    obtain_files("fixture", spec, self.root)
                self.assertFalse((self.root / "sources/fixture-aaaaaaaaaaaa").exists())

    def test_bad_file_selection_is_rejected_before_fetch(self):
        for name in ("../escape", "/absolute", "drive:escape", "back\\slash", "include/HEADER.h",
                     "include/header.h/child", ".artbox-source.json"):
            spec, _ = self.file_spec()
            spec["files"].append({"path": name, "bytes": 1, "sha256": "0" * 64})
            with self.subTest(name=name), patch("sources.subprocess.run") as fetch:
                with self.assertRaises(RuntimeError):
                    obtain_files("fixture", spec, self.root)
                fetch.assert_not_called()


if __name__ == "__main__":
    os.environ.update(environment())
    unittest.main()
