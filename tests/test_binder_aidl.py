"""Pinned compiler admission and generated-output integrity; no host tool executes."""
import sys
sys.dont_write_bytecode = True
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import environment
from binder_aidl import native_profile, safe_relative, verify_file, generated_files, fetch_file


class AidlInputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_native_host_selection_never_selects_foreign_architecture(self):
        self.assertEqual(native_profile('Darwin', 'arm64'), 'darwin-arm64')
        self.assertEqual(native_profile('Darwin', 'x86_64'), 'darwin-x86_64')
        self.assertEqual(native_profile('Linux', 'AMD64'), 'linux-x86_64')
        for system, machine in [('Windows', 'AMD64'), ('Linux', 'aarch64'), ('Darwin', 'i386')]:
            with self.assertRaises(RuntimeError):
                native_profile(system, machine)

    def test_paths_are_relative_and_unambiguous(self):
        self.assertEqual(safe_relative('darwin-x86/lib64/libc++.dylib').as_posix(),
                         'darwin-x86/lib64/libc++.dylib')
        for name in ('', '.', '/bin/tool', '../tool', 'a/../tool', 'a\\tool', 'C:/tool', 'a//tool', './tool'):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                safe_relative(name)

    def item(self, data=b'compiler input'):
        return dict(path='file', bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                    git_blob=hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest())

    def test_rechecks_size_hash_and_git_blob_before_reusing_cache(self):
        target = self.root / 'file'
        item = self.item()
        target.write_bytes(b'compiler input')
        verify_file(target, item)
        for mutation in (b'broken', b'compiler Input'):
            target.write_bytes(mutation)
            with self.assertRaises(RuntimeError): verify_file(target, item)
        target.write_bytes(b'compiler input')
        with self.assertRaises(RuntimeError): verify_file(target, {**item, 'git_blob': '0' * 40})
        with patch('binder_aidl.subprocess.run', side_effect=AssertionError('No download expected')):
            self.assertEqual(fetch_file(self.root, 'owner/repo', 'a' * 40, item), target)
            target.write_bytes(b'compiler Input')
            with self.assertRaises(RuntimeError): fetch_file(self.root, 'owner/repo', 'a' * 40, item)

    def test_failed_download_never_publishes_unverified_bytes(self):
        def download(command, **kwargs):
            kwargs['stdout'].write(b'unreviewed')
            return None
        with patch('binder_aidl.subprocess.run', side_effect=download):
            with self.assertRaises(RuntimeError):
                fetch_file(self.root, 'owner/repo', 'a' * 40, self.item())
        self.assertEqual(list(self.root.iterdir()), [])

    def test_generated_manifest_requires_every_interface_and_no_extra_file(self):
        interfaces = ('IServiceManager', 'IServiceCallback', 'IClientCallback')
        parcelables = ('ConnectionInfo', 'ServiceDebugInfo')
        names = ['src/android/os/' + n + '.cpp' for n in interfaces + parcelables]
        names += ['include/android/os/' + n + '.h' for n in interfaces + parcelables]
        names += ['include/android/os/' + p + n[1:] + '.h' for n in interfaces for p in ('Bn', 'Bp')]
        names += ['include/android/os/' + p + n + '.h' for n in parcelables for p in ('Bn', 'Bp')]
        for name in names:
            p = self.root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(b'generated')
        self.assertEqual(len(generated_files(self.root)), 20)
        extra = self.root / 'host-compiler'; extra.write_bytes(b'not an output')
        with self.assertRaises(RuntimeError): generated_files(self.root)
        extra.unlink()
        (self.root / names[0]).unlink()
        with self.assertRaises(RuntimeError): generated_files(self.root)


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
