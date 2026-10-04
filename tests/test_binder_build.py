"""Reject changed generator inputs and accidental host objects before packaging."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import ROOT, environment
from binder_aidl import COMPILER_OPTIONS, INTERFACES, PARCELABLES, generated_files
from build_binder import verify_aidl, verify_object_architecture, symbol_inventory, ndk_notices


class BinderBuild(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def aidl_fixture(self):
        names = ['src/android/os/' + n + '.cpp' for n in INTERFACES + PARCELABLES]
        names += ['include/android/os/' + n + '.h' for n in INTERFACES + PARCELABLES]
        names += ['include/android/os/' + p + n[1:] + '.h' for n in INTERFACES for p in ('Bn', 'Bp')]
        names += ['include/android/os/' + p + n + '.h' for n in PARCELABLES for p in ('Bn', 'Bp')]
        for name in names:
            path = self.root / 'generated' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'generated fixture\n')
        tool = json.loads((ROOT / 'third_party/binder/aidl-tools.json').read_text(encoding='utf-8'))
        source = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))['binder-aidl']
        profile = tool['profiles']['darwin-arm64']
        exe = profile['directory'] + '/' + profile['executable']
        record = dict(profile='darwin-arm64', tool_commit=tool['commit'],
            compiler_source_commit=tool['compiler_source_commit'], compiler_options=list(COMPILER_OPTIONS),
            compiler_sha256=next(i['sha256'] for i in tool['files'] if i['path'] == exe),
            source_files={i['path']: i['sha256'] for i in source['files']},
            native_execution_verified=True, deterministic_generation_verified=True, malformed_inputs_rejected=2,
            generated_files=generated_files(self.root / 'generated'))
        self.save_record(record)
        return record

    def save_record(self, record):
        (self.root / 'generation.json').write_text(json.dumps(record), encoding='utf-8')

    def test_verified_generation_is_usable_on_a_different_build_host(self):
        record = self.aidl_fixture()
        self.assertEqual(verify_aidl(self.root), record)

    def test_generation_requires_the_pinned_compiler_and_source(self):
        original = self.aidl_fixture()
        changes = dict(tool_commit='0' * 40, compiler_source_commit='0' * 40, compiler_sha256='0' * 64,
            compiler_options=['--lang=cpp'], profile='windows-x86_64', source_files={},
            native_execution_verified=False, deterministic_generation_verified=False, malformed_inputs_rejected=0)
        for key, value in changes.items():
            with self.subTest(field=key):
                record = copy.deepcopy(original); record[key] = value; self.save_record(record)
                with self.assertRaises(RuntimeError): verify_aidl(self.root)

    def test_missing_extra_and_changed_generated_files_fail(self):
        self.aidl_fixture()
        path = self.root / 'generated/src/android/os/IServiceManager.cpp'
        original = path.read_bytes(); path.write_bytes(b'changed\n')
        with self.assertRaises(RuntimeError): verify_aidl(self.root)
        path.unlink()
        with self.assertRaises(RuntimeError): verify_aidl(self.root)
        path.write_bytes(original)
        (path.parent / 'unreviewed.cpp').write_bytes(b'extra\n')
        with self.assertRaises(RuntimeError): verify_aidl(self.root)

    def test_only_little_endian_arm64_relocatable_objects(self):
        header = bytearray(64); header[:7] = b'\x7fELF\x02\x01\x01'
        struct.pack_into('<HHI', header, 16, 1, 183, 1)
        path = self.root / 'unit.o'; path.write_bytes(header)
        verify_object_architecture(path)
        for offset, value in ((4, 1), (5, 2), (16, 3), (18, 62), (20, 0)):
            changed = header.copy(); changed[offset] = value; path.write_bytes(changed)
            with self.subTest(offset=offset), self.assertRaises(RuntimeError): verify_object_architecture(path)
        path.write_bytes(header[:24])
        with self.assertRaises(RuntimeError): verify_object_architecture(path)

    def test_symbols_resolve_across_archives_without_hiding_external_imports(self):
        inventory = symbol_inventory('one.o:\nprovided T 0 4\nneeded U 0 0\noptional w 0 0\n'
                                     'two.o:\nneeded W 0 4\nexternal U 0 0\n')
        self.assertEqual(inventory['required_external'], ['external'])
        self.assertEqual(inventory['optional_external'], ['optional'])
        self.assertEqual(inventory['defined'], ['needed', 'provided'])

    def test_distinct_ndk_root_and_toolchain_notices_are_retained(self):
        toolchain = self.root / 'toolchain'; toolchain.mkdir()
        (self.root / 'NOTICE').write_bytes(b'NDK distribution licenses')
        (toolchain / 'NOTICE').write_bytes(b'LLVM toolchain licenses')
        pin = dict(notice_sha256=hashlib.sha256(b'LLVM toolchain licenses').hexdigest())
        with patch('build_binder.read_json', return_value=pin):
            notices = ndk_notices(self.root, toolchain)
            self.assertEqual(set(notices.values()), {self.root / 'NOTICE', toolchain / 'NOTICE'})
            (toolchain / 'NOTICE').write_bytes(b'changed license')
            with self.assertRaises(RuntimeError): ndk_notices(self.root, toolchain)


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
