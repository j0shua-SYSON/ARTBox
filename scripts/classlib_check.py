"""Shared build and negative cases for AOSP implementation DEX verification."""
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import time
import zipfile
import zlib
from environment import ROOT


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_inputs(root, directory):
    result = json.loads((root / 'result.json').read_text(encoding='utf-8'))
    attempt = result['attempt']
    if not isinstance(attempt, str) or not attempt.startswith('attempt-') or Path(attempt).name != attempt:
        raise RuntimeError('Invalid class-library build attempt')
    report_path = root / attempt / 'report.json'
    report = json.loads(report_path.read_text(encoding='utf-8'))
    if report != result['report'] or report['runtime_executed']:
        raise RuntimeError('Class-library report differs from the build result')
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    config = json.loads((ROOT / 'third_party/art/classlib.json').read_text(encoding='utf-8'))
    expected_sources = {n: specs[n] for n in config['java_sources'] + config['build_sources']}
    if report['sources'] != expected_sources:
        raise RuntimeError('Class-library source provenance differs')
    for name, expected in report['project_sources'].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError('Class-library build input changed: ' + name)
    archive_path = root / attempt / 'implementation-dex.zip'
    if digest(archive_path) != report['artifacts']['implementation-dex.zip']['sha256']:
        raise RuntimeError('Class-library DEX archive changed')
    directory.mkdir(parents=True, exist_ok=True)
    paths = []
    with zipfile.ZipFile(archive_path) as archive:
        if sorted(archive.namelist()) != ['classes.dex', 'classes2.dex']:
            raise RuntimeError('Unexpected class-library DEX archive entries')
        for item in report['dex_files']:
            contents = archive.read(item['name'])
            if len(contents) != item['bytes'] or hashlib.sha256(contents).hexdigest() != item['sha256']:
                raise RuntimeError('Class-library DEX input changed')
            path = directory / item['name']
            path.write_bytes(contents)
            paths.append(path)
    # Corrupt integrity alone, then structural metadata with corrected integrity.
    data = bytearray(paths[0].read_bytes())
    data[8] ^= 1
    checksum = directory / 'bad-checksum.dex'
    checksum.write_bytes(data)
    data = bytearray(paths[0].read_bytes())
    classes_offset = struct.unpack_from('<I', data, 100)[0]
    type_count = struct.unpack_from('<I', data, 64)[0]
    struct.pack_into('<I', data, classes_offset, type_count)
    data[12:32] = hashlib.sha1(data[32:]).digest()
    struct.pack_into('<I', data, 8, zlib.adler32(data[12:]))
    structural = directory / 'class-index-out-of-range.dex'
    structural.write_bytes(data)
    cases = {'implementation': (paths, 0, None),
             'bad-checksum': ([checksum, paths[1]], 2, 'AOSP DEX verifier:'),
             'class-index-out-of-range': ([structural, paths[1]], 2, 'AOSP DEX verifier:'),
             'duplicate-class-set': ([*paths, paths[0]], 3, 'Class-library contract: duplicate class'),
             'missing-secondary-dex': ([paths[0]], 3, 'Class-library contract: incomplete class set')}
    record = {'build_report_sha256': digest(report_path), 'dex_files': report['dex_files'],
              'runtime_executed': False, 'device_execution_verified': False,
              'project_sources': {p: digest(ROOT / p) for p in
                                  ('scripts/classlib_check.py', 'fixtures/art-classlib/check.cpp')}}
    return cases, record


def build_target(compiler, flags, link_flags, objects, target, directory, notices, cases, compile_units, macho):
    directory.mkdir(parents=True, exist_ok=True)
    target_flags = [] if target == 'ios' else ['-DARTBOX_CLASSLIB_EXECUTABLE']
    check, metadata = compile_units(compiler, flags + target_flags,
                                    [('classlib-check', ROOT / 'fixtures/art-classlib/check.cpp')],
                                    directory / 'objects', 1)
    def run(*args):
        subprocess.run([str(a) for a in args], check=True)
    record = {'objects': metadata}
    if target == 'ios':
        name = 'ARTBoxClassLibrary'
        framework = directory / (name + '.framework')
        framework.mkdir(exist_ok=True)
        binary = framework / name
        run(*compiler, *link_flags, '-dynamiclib', *objects, *check, '-lz', '-Wl,-dead_strip',
            '-Wl,-exported_symbol,_artbox_classlib_inspect',
            '-Wl,-install_name,@rpath/' + name + '.framework/' + name, '-o', binary)
        info = {'CFBundleIdentifier': 'org.artbox.ClassLibrary', 'CFBundleExecutable': name,
                'CFBundleName': name, 'CFBundlePackageType': 'FMWK', 'CFBundleVersion': '1',
                'CFBundleShortVersionString': '1.0', 'MinimumOSVersion': '15.0',
                'CFBundleSupportedPlatforms': ['iPhoneOS']}
        (framework / 'Info.plist').write_bytes(plistlib.dumps(info))
        for notice in notices.glob('*.txt'):
            (framework / notice.name).write_bytes(notice.read_bytes())
        run('codesign', '--force', '--sign', '-', '--timestamp=none', framework)
        record['signed_binary'] = macho(binary, 2, None, kind=6)
        return record
    binary = directory / 'classlib-check'
    run(*compiler, *link_flags, *objects, *check, '-lz', '-pthread',
        '-Wl,-dead_strip' if target == 'macos' else '-Wl,--gc-sections', '-o', binary)
    if target == 'macos':
        run('codesign', '--force', '--sign', '-', '--timestamp=none', binary)
        record['signed_binary'] = macho(binary, 1, 2**32)
    record['binary_sha256'] = digest(binary)
    record['cases'] = {}
    for name, (inputs, expected, diagnostic) in cases.items():
        start = time.monotonic()
        process = subprocess.run([str(binary), *[str(p) for p in inputs]], capture_output=True,
                                 encoding='utf-8', timeout=120)
        seconds = time.monotonic() - start
        (directory / (name + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        if process.returncode != expected or diagnostic and diagnostic not in process.stderr:
            raise RuntimeError(f'Class-library case {name} failed ({process.returncode}); see {directory}')
        value = json.loads(process.stdout)
        if value['result'] != expected or value['dex_executed'] or value['dex_verified'] != (expected == 0):
            raise RuntimeError('Invalid class-library result: ' + name)
        if expected == 0 and value['classes'] != 7309:
            raise RuntimeError('Class-library verifier reported the wrong class count')
        record['cases'][name] = {'result': value, 'seconds': seconds,
                                 'inputs': [digest(p) for p in inputs]}
    return record
