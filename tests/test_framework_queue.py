"""Reject mutable release flags, reference payloads and changed compiler inputs."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import ROOT, environment
from build_framework_queue import core_jar, digest, readonly_flag, validate_classes, verify_input


class QueueInputs(unittest.TestCase):
    def test_exact_readonly_flag_tuple(self):
        text = b'flag_value { package: "android.os" name: "message_queue_tail_tracking" state: ENABLED permission: READ_ONLY }'
        self.assertTrue(readonly_flag(text))
        self.assertFalse(readonly_flag(text.replace(b'ENABLED', b'DISABLED')))
        for changed in (text.replace(b'READ_ONLY', b'READ_WRITE'), text+text,
                        text.replace(b'android.os', b'android.fake'), text.replace(b'ENABLED', b'UNKNOWN'),
                        text.replace(b'flag_value', b'flag'), text.replace(b' }', b' extra: true }')):
            with self.subTest(changed=changed), self.assertRaises(ValueError): readonly_flag(changed)

    def test_only_source_built_queue_classes_enter_payload(self):
        expected = json.loads((ROOT/'third_party/framework/queue.json').read_bytes())['java_classes']
        validate_classes(expected, expected)
        for names in (expected[:-1], expected+[expected[0]], expected+['android/app/Activity.class'],
                      expected+['java/lang/Object.class'], expected+['org/robolectric/Shadows.class']):
            with self.subTest(names=names), self.assertRaises(ValueError): validate_classes(names, expected)

    def test_size_hash_and_git_blob_all_match(self):
        data = b'original source\n'
        pin = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                   git_blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'source'; path.write_bytes(data)
            verify_input(path, pin)
            for field, value in [('bytes', len(data)+1), ('sha256', '0'*64), ('git_blob', '0'*40)]:
                with self.subTest(field=field), self.assertRaises(ValueError): verify_input(path, {**pin, field:value})
            path.write_bytes(b'x'*len(data))
            with self.assertRaises(ValueError): verify_input(path, pin)

    def test_boot_dependency_rejects_stale_or_incomplete_provenance(self):
        config_path = ROOT/'third_party/art/classlib.json'
        config = json.loads(config_path.read_bytes())
        catalog = json.loads((ROOT/'third_party/sources.json').read_bytes())
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            attempt = directory/'attempt-test'; attempt.mkdir()
            jar = attempt/'implementation-classes.jar'
            with zipfile.ZipFile(jar, 'w') as archive:
                names = [name+'.class' for name in config['required_classes']]
                names += [f'fixture/Class{n}.class' for n in range(config['expected_class_files']-len(names))]
                for name in names: archive.writestr(name, b'fixture')
            report = dict(configuration_sha256=digest(config_path), class_files=len(names),
                sources={name:catalog[name] for name in config['java_sources']+config['build_sources']},
                artifacts={jar.name:dict(bytes=jar.stat().st_size, sha256=digest(jar))})
            def save(value):
                (directory/'result.json').write_text(json.dumps(value), encoding='utf-8')
            original = dict(attempt=attempt.name, report=report)
            save(original)
            self.assertEqual(core_jar(directory)[0], jar)
            changes = [dict(configuration_sha256='0'*64), dict(sources={}), dict(class_files=1),
                       dict(artifacts={jar.name:dict(bytes=jar.stat().st_size, sha256='0'*64)})]
            for change in changes:
                save(dict(attempt=attempt.name, report={**report, **change}))
                with self.subTest(change=list(change)), self.assertRaises(ValueError): core_jar(directory)
            for escape in ('..', '../attempt-test', '/attempt-test', 'attempt-test/child'):
                save(dict(attempt=escape, report=report))
                with self.subTest(escape=escape), self.assertRaises(ValueError): core_jar(directory)


if __name__ == '__main__':
    os.environ.update(environment())
    unittest.main()
