"""Generate AOSP servicemanager's C++ interfaces with a pinned native host tool."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import tempfile
from urllib.parse import quote
import xml.etree.ElementTree as ET

from environment import ROOT, environment
from sources import obtain

INTERFACES = ('IServiceManager', 'IServiceCallback', 'IClientCallback')
PARCELABLES = ('ConnectionInfo', 'ServiceDebugInfo')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(name):
    path = PurePosixPath(name)
    if not name or not path.parts or path.is_absolute() or path.as_posix() != name or '..' in path.parts or ':' in name or '\\' in name:
        raise RuntimeError('Compiler input path must be a normalized relative path')
    return path


def native_profile(system=None, machine=None):
    system, machine = system or platform.system(), (machine or platform.machine()).lower()
    machine = {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(machine, machine)
    key = system.lower() + '-' + machine
    if key not in ('darwin-arm64', 'darwin-x86_64', 'linux-x86_64'):
        raise RuntimeError('No reviewed native AIDL binary for this host; use --prepare-only --profile or generate on Mac/Linux x86_64')
    return key


def verify_file(path, item):
    if path.is_symlink() or not path.is_file() or path.stat().st_size != item['bytes']:
        raise RuntimeError('Pinned compiler input is missing or has the wrong size')
    data = path.read_bytes()
    if digest(data) != item['sha256'] or hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() != item['git_blob']:
        raise RuntimeError('Pinned compiler input differs from its SHA-256 or Git blob')


def fetch_file(root, repository, commit, item):
    if not re.fullmatch(r'[\w.-]+/[\w.-]+', repository) or not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise RuntimeError('Invalid pinned compiler repository or commit')
    target = root / safe_relative(item['path'])
    if not target.resolve().is_relative_to(root.resolve()):
        raise RuntimeError('Compiler cache path escapes its configured directory')
    if target.exists() or target.is_symlink():
        verify_file(target, item)
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.aidl-', delete=False) as output:
            temporary = Path(output.name)
            url = f'https://raw.githubusercontent.com/{repository}/{commit}/{quote(item["path"], safe="/")}'
            subprocess.run(['gh', 'api', url, '-H', 'Authorization:'], stdout=output, check=True)
        verify_file(temporary, item)
        temporary.replace(target)
    finally:
        if temporary is not None and temporary.exists(): temporary.unlink()
    return target


def prepare(spec, profile, cache):
    selected = spec['profiles'][profile]
    root = cache / 'toolchains' / ('binder-aidl-' + spec['commit'][:12])
    if root.is_symlink(): raise RuntimeError('Compiler cache must not be a symlink')
    directory = safe_relative(selected['directory'])
    wanted = {str(directory / safe_relative(selected[key])) for key in ('executable', 'library')}
    records = {item['path']: item for item in spec['files']}
    if not wanted.issubset(records): raise RuntimeError('Compiler profile omits its pinned executable or library')
    paths = {}
    for name, item in records.items():
        if name in wanted or '/' not in name:
            paths[name] = fetch_file(root, spec['repository'], spec['commit'], item)
    manifest = ET.parse(paths['manifest.xml']).getroot()
    revisions = {p.get('path'): p.get('revision') for p in manifest.findall('project')}
    if revisions.get('system/tools/aidl') != spec['compiler_source_commit']:
        raise RuntimeError('Compiler source revision differs from its build manifest')
    for item in spec['notices']:
        if revisions.get(item['project']) != item['commit']:
            raise RuntimeError('Compiler notice revision differs from its build manifest')
        local = root / 'notices' / safe_relative(item['project'])
        paths['notices/' + item['project'] + '/' + item['path']] = fetch_file(local, item['repository'], item['commit'], item)
    executable = paths[str(directory / selected['executable'])]
    executable.chmod(executable.stat().st_mode | 0o111)
    return executable, paths


def generated_files(root):
    expected = {'src/android/os/' + n + '.cpp' for n in INTERFACES + PARCELABLES}
    expected |= {'include/android/os/' + n + '.h' for n in INTERFACES + PARCELABLES}
    expected |= {'include/android/os/' + p + n[1:] + '.h' for n in INTERFACES for p in ('Bn', 'Bp')}
    found = {p.relative_to(root).as_posix(): p for p in root.rglob('*') if p.is_file() or p.is_symlink()}
    if set(found) != expected or any(p.is_symlink() or not p.stat().st_size for p in found.values()):
        raise RuntimeError('Generated Binder file set is incomplete or contains unexpected files')
    return {name: digest(path.read_bytes()) for name, path in sorted(found.items())}


def compile_aidl(executable, source, destination, inputs, log):
    destination.mkdir(parents=True)
    (destination / 'src').mkdir(); (destination / 'include').mkdir()
    args = [str(executable), '--lang=cpp', '--structured', '--min_sdk_version=29',
            '--out=' + str(destination / 'src'), '--header_out=' + str(destination / 'include'),
            '-I', '.', *inputs]
    result = subprocess.run(args, cwd=source, capture_output=True, timeout=60)
    with log.open('ab') as output:
        output.write(result.stdout + result.stderr)
    return result


def generate(executable, source, output):
    output.mkdir(parents=True, exist_ok=True)
    log = output / 'compiler.log'; log.write_bytes(b'')
    record = output / 'generation.json'
    if record.exists(): record.unlink()
    inputs = ['android/os/' + n + '.aidl' for n in INTERFACES + PARCELABLES]
    with tempfile.TemporaryDirectory(prefix='binder-aidl-', dir=os.environ['ARTBOX_TEMP_DIR']) as temporary:
        stage = Path(temporary)
        for round_name in ('first', 'second'):
            compile_aidl(executable, source, stage / round_name, inputs, log).check_returncode()
        first = generated_files(stage / 'first')
        if generated_files(stage / 'second') != first:
            raise RuntimeError('AIDL output changes with the generation directory')
        for index, text in enumerate(('package android.os; interface IInvalid { void broken( ; }',
                                     'package android.os; interface IInvalid { MissingType broken(); }')):
            invalid = stage / ('invalid-' + str(index))
            (invalid / 'android/os').mkdir(parents=True)
            (invalid / 'android/os/IInvalid.aidl').write_text(text, encoding='utf-8')
            result = compile_aidl(executable, invalid, stage / ('rejected-' + str(index)), ['android/os/IInvalid.aidl'], log)
            if result.returncode <= 0 or b'ERROR:' not in result.stdout + result.stderr:
                raise RuntimeError('AIDL compiler did not reject the malformed input with a diagnostic')
        generated = output / 'generated'
        if generated.exists():
            if generated_files(generated) != first:
                raise RuntimeError('Existing generated bindings differ; use a fresh --output directory')
        else: shutil.copytree(stage / 'first', generated)
    return first


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', action='store_true', help='Fetch and verify inputs without executing a compiler')
    parser.add_argument('--profile', choices=('darwin-arm64', 'darwin-x86_64', 'linux-x86_64'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    profile = args.profile or native_profile()
    if not args.prepare_only and profile != native_profile():
        parser.error('Execution requires the native host profile; cross-host preparation needs --prepare-only')
    spec = json.loads((ROOT / 'third_party/binder/aidl-tools.json').read_text())
    executable, tools = prepare(spec, profile, Path(os.environ['ARTBOX_CACHE_DIR']))
    source = obtain('binder-aidl')
    if args.prepare_only:
        print(json.dumps(dict(profile=profile, inputs_verified=True, compiler_executed=False)))
        return
    output = (args.output or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/binder-aidl' / profile).resolve()
    hashes = generate(executable, source / 'libs/binder/aidl', output)
    # Artifact source/notices exclude host binaries. The runtime build will
    # consume generated C++, while these originals retain their AOSP licenses.
    source_spec = json.loads((ROOT / 'third_party/sources.json').read_text())['binder-aidl']
    for item in source_spec['files']:
        target = output / 'source-inputs' / item['path']; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / item['path'], target)
    for name, path in tools.items():
        if name.startswith('notices/') or '/' not in name:
            target = output / 'tool-provenance' / name; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    project_paths = ('scripts/binder_aidl.py', 'scripts/sources.py', 'scripts/environment.py',
                     'third_party/binder/aidl-tools.json', 'third_party/sources.json', 'tests/test_binder_aidl.py')
    record = dict(profile=profile, compiler_source_commit=spec['compiler_source_commit'],
        compiler_sha256=digest(executable.read_bytes()), tool_commit=spec['commit'],
        project_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        project_files={name:digest((ROOT / name).read_bytes()) for name in project_paths},
        source_files={item['path']: item['sha256'] for item in source_spec['files']}, generated_files=hashes,
        notice_files={name:digest(path.read_bytes()) for name,path in tools.items() if name.startswith('notices/')},
        native_execution_verified=True, deterministic_generation_verified=True, malformed_inputs_rejected=2,
        compiler_redistributed=False, servicemanager_execution_verified=False, device_execution_verified=False)
    (output / 'generation.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(profile=profile, generated_files=len(hashes), malformed_inputs_rejected=2,
                         native_execution_verified=True, servicemanager_execution_verified=False)))


if __name__ == '__main__':
    main()
