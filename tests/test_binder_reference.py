"""Malformed package/cache controls; these tests never load a native module."""
import sys
sys.dont_write_bytecode = True

import hashlib
import io
from contextlib import redirect_stderr
import json
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import environment
from binder_reference import main, prepare, unpack_selected


def ar_member(name, data):
    header = f'{name + "/":<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(data):<10}`\n'.encode('ascii')
    assert len(header) == 60
    return header + data + (b'\n' if len(data) % 2 else b'')


class ReferencePackages(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.target = self.root / 'selected'
        self.target.mkdir()
        self.archive = self.root / 'test.deb'
        self.payloads = {'lib/modules/test/binder.ko': b'module bytes', 'usr/share/doc/test/copyright': b'notice bytes'}
        self.selected = {name: {'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                         for name, data in self.payloads.items()}

    def tar(self, entries=None):
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode='w') as bundle:
            for name, data, kind in entries or [(n, d, tarfile.REGTYPE) for n, d in self.payloads.items()]:
                info = tarfile.TarInfo('./' + name)
                info.type = kind
                info.size = len(data) if kind == tarfile.REGTYPE else 0
                info.linkname = '/outside'
                bundle.addfile(info, io.BytesIO(data) if kind == tarfile.REGTYPE else None)
        return output.getvalue()

    def package(self, data=None):
        data = self.tar() if data is None else data
        return b'!<arch>\n' + ar_member('debian-binary', b'2.0\n') + ar_member('data.tar', data)

    def test_extract_only_regular_selected_members(self):
        entries = [(n, d, tarfile.REGTYPE) for n, d in self.payloads.items()]
        entries += [('../../unselected', b'do not extract', tarfile.REGTYPE)]
        self.archive.write_bytes(self.package(self.tar(entries)))
        unpack_selected(self.archive, self.target, self.selected)
        self.assertEqual({p.name: p.read_bytes() for p in self.target.iterdir()},
                         {Path(n).name: d for n, d in self.payloads.items()})

    def test_truncated_and_duplicate_ar_members(self):
        valid = self.package()
        for data in (b'bad', valid[:20], valid[:-700], valid + ar_member('data.tar', self.tar())):
            with self.subTest(bytes=len(data)):
                self.archive.write_bytes(data)
                with self.assertRaises(RuntimeError):
                    unpack_selected(self.archive, self.target, self.selected)

    def test_selected_member_must_be_regular_and_unique(self):
        name = next(iter(self.payloads))
        for entries in (
            [(name, b'', tarfile.SYMTYPE)],
            [(name, self.payloads[name], tarfile.REGTYPE)] * 2,
        ):
            self.archive.write_bytes(self.package(self.tar(entries)))
            with self.assertRaises(RuntimeError):
                unpack_selected(self.archive, self.target, self.selected)

    def test_changed_bytes_and_missing_notice_rejected(self):
        name = next(iter(self.payloads))
        for entries in (
            [(name, b'x' * len(self.payloads[name]), tarfile.REGTYPE)],
            [(name, self.payloads[name], tarfile.REGTYPE)],
        ):
            self.archive.write_bytes(self.package(self.tar(entries)))
            with self.assertRaises(RuntimeError):
                unpack_selected(self.archive, self.target, self.selected)

    def spec(self):
        data = self.package()
        return {'package': 'linux-modules-test', 'version': '1.0',
                'url': 'https://archive.ubuntu.com/ubuntu/test_arm64.deb',
                'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                'module': self.selected['lib/modules/test/binder.ko'],
                'notice': self.selected['usr/share/doc/test/copyright']}

    def test_cache_rechecks_module_and_provenance(self):
        spec = self.spec()
        cache = self.root / 'cache'
        downloads = cache / 'downloads/binder-reference'
        downloads.mkdir(parents=True)
        (downloads / 'test_arm64.deb').write_bytes(self.package())
        with patch('binder_reference.subprocess.run', side_effect=AssertionError('No network expected')):
            installed = prepare(spec, cache)
            self.assertEqual(prepare(spec, cache), installed)
            (installed / 'binder.ko').write_bytes(b'wrong bytes!')
            with self.assertRaises(RuntimeError):
                prepare(spec, cache)
            (installed / 'binder.ko').write_bytes(self.payloads[spec['module']['path']])
            (installed / 'pin.json').write_text(json.dumps({'different': True}))
            with self.assertRaises(RuntimeError):
                prepare(spec, cache)

    def test_unsafe_source_pin_rejected_before_download(self):
        for mutation in ('identity', 'member'):
            spec = self.spec()
            if mutation == 'identity':
                spec['package'] = '../outside'
            else:
                spec['module'] = {**spec['module'], 'path': '../outside'}
            with patch('binder_reference.subprocess.run', side_effect=AssertionError('No network expected')):
                with self.assertRaises(RuntimeError):
                    prepare(spec, self.root / 'cache')

    def test_native_execution_requires_disposable_host_acknowledgement(self):
        errors = io.StringIO()
        with patch.object(sys, 'argv', ['binder_reference.py', '--run-native']), redirect_stderr(errors):
            with patch('binder_reference.prepare', side_effect=AssertionError('No preparation before acknowledgement')):
                with self.assertRaises(SystemExit) as stopped:
                    main()
        self.assertEqual(stopped.exception.code, 2)
        self.assertIn('--disposable-host', errors.getvalue())

    def test_driver_comparison_requires_native_execution(self):
        errors = io.StringIO()
        with patch.object(sys, 'argv', ['binder_reference.py', '--compare-driver']), redirect_stderr(errors):
            with patch('binder_reference.prepare', side_effect=AssertionError('No preparation before native requirement')):
                with self.assertRaises(SystemExit) as stopped:
                    main()
        self.assertEqual(stopped.exception.code, 2)
        self.assertIn('--run-native', errors.getvalue())


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
