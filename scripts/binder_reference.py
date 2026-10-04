"""Prepare a pinned native Linux Binder reference without installing packages.

Preparation works on every host. --run-native --disposable-host mounts a private
binderfs on the matching Linux host using existing sudo and module tools. The
pinned module has no exit hook and remains loaded until that host is discarded.
No kernel is booted, emulated, installed or included in an Apple artifact.
"""
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
import tarfile
import tempfile
import time
from urllib.parse import urlsplit

from environment import ROOT, environment


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def check_file(path, spec):
    if path.is_symlink() or not path.is_file() or path.stat().st_size != spec['bytes'] or digest(path) != spec['sha256']:
        raise RuntimeError(f'Pinned Binder reference bytes differ: {path.name}')


class MemberReader:
    """Expose only the data member of a validated Debian ar archive."""
    def __init__(self, source, remaining):
        self.source, self.remaining = source, remaining

    def read(self, size=-1):
        size = self.remaining if size < 0 else min(size, self.remaining)
        data = self.source.read(size)
        self.remaining -= len(data)
        return data


def unpack_selected(archive, target, selected):
    with archive.open('rb') as source:
        if source.read(8) != b'!<arch>\n':
            raise RuntimeError('Expected a Debian ar archive')
        data_member = None
        while header := source.read(60):
            if len(header) != 60 or header[58:] != b'`\n':
                raise RuntimeError('Invalid Debian archive header')
            name = header[:16].decode('ascii').strip().removesuffix('/')
            size = int(header[48:58].decode('ascii').strip())
            offset = source.tell()
            if size < 0 or offset + size > archive.stat().st_size:
                raise RuntimeError('Debian member escapes archive')
            if name in ('data.tar', 'data.tar.gz', 'data.tar.xz'):
                if data_member is not None:
                    raise RuntimeError('Duplicate Debian data member')
                data_member = (offset, size)
            source.seek(size + size % 2, 1)
        if data_member is None:
            raise RuntimeError('No supported Debian data member; review the new package format')
        source.seek(data_member[0])
        found = set()
        with tarfile.open(fileobj=MemberReader(source, data_member[1]), mode='r|*') as bundle:
            for member in bundle:
                name = member.name.removeprefix('./')
                if name not in selected:
                    continue
                spec = selected[name]
                if name in found or not member.isfile() or member.size != spec['bytes']:
                    raise RuntimeError('Duplicate or non-regular Binder module/notice member')
                found.add(name)
                output = target / PurePosixPath(name).name
                with bundle.extractfile(member) as payload, output.open('wb') as destination:
                    shutil.copyfileobj(payload, destination, 1024 * 1024)
                check_file(output, spec)
        if found != set(selected):
            raise RuntimeError('Module/notice missing from pinned Debian package')


def prepare(spec, cache):
    if not re.fullmatch(r'[a-z0-9.+-]+', spec['package']) or not re.fullmatch(r'[a-z0-9.+~-]+', spec['version']):
        raise RuntimeError('Unsafe reference package identity')
    selected = {spec[key]['path']: spec[key] for key in ('module', 'notice')}
    if len(selected) != 2 or len({PurePosixPath(p).name for p in selected}) != 2:
        raise RuntimeError('Reference module and notice must be distinct')
    for name in selected:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name:
            raise RuntimeError('Unsafe pinned reference member')
    downloads = cache / 'downloads/binder-reference'
    downloads.mkdir(parents=True, exist_ok=True)
    archive_name = PurePosixPath(urlsplit(spec['url']).path).name
    if not re.fullmatch(r'[A-Za-z0-9._+~-]+\.deb', archive_name):
        raise RuntimeError('Unsafe reference archive filename')
    archive = downloads / archive_name
    if not archive.is_file():
        partial = archive.with_suffix('.part')
        if partial.is_symlink():
            raise RuntimeError('Refusing a redirected package download')
        with partial.open('wb') as output:
            subprocess.run(['gh', 'api', spec['url'], '-H', 'Authorization:'], stdout=output, check=True, timeout=180)
        check_file(partial, spec)
        partial.replace(archive)
    check_file(archive, spec)
    parent = cache / 'binder-reference'
    parent.mkdir(parents=True, exist_ok=True)
    installed = parent / (spec['package'] + '-' + spec['version'])
    if installed.is_symlink():
        raise RuntimeError('Refusing redirected reference directory')
    if not installed.exists():
        with tempfile.TemporaryDirectory(prefix='extract-', dir=parent) as temporary:
            staged = Path(temporary) / 'payload'
            staged.mkdir()
            unpack_selected(archive, staged, selected)
            (staged / 'pin.json').write_text(json.dumps(spec, indent=2) + '\n', encoding='utf-8')
            staged.replace(installed)
    if json.loads((installed / 'pin.json').read_text()) != spec:
        raise RuntimeError('Reference cache provenance differs')
    for name, item in selected.items():
        check_file(installed / PurePosixPath(name).name, item)
    return installed


def run_native(spec, installed, build):
    if sys.platform != 'linux' or platform.release() != spec['kernel'] or platform.machine() != spec['architecture']:
        raise RuntimeError('Native execution requires the exact pinned Linux kernel and architecture')
    required = ('cc', 'sudo', 'insmod', 'modinfo', 'mount', 'umount')
    commands = {name: shutil.which(name) for name in required}
    if not all(commands.values()):
        raise RuntimeError('Native reference needs existing compiler, sudo and module/mount tools; nothing was installed')
    if Path('/sys/module/binder_linux').exists():
        raise RuntimeError('Use an isolated runner: an existing Binder module will not be replaced')
    build.mkdir(parents=True, exist_ok=True)
    mount = build / 'binderfs'
    if mount.is_symlink() or (mount.exists() and any(mount.iterdir())):
        raise RuntimeError('Private binderfs requires an empty owned mount directory')
    mount.mkdir(exist_ok=True)
    executable = build / 'binder-device-reference'
    module = installed / PurePosixPath(spec['module']['path']).name
    logs = []

    def invoke(*args, timeout=60):
        result = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=timeout)
        logs.append(' '.join(str(x) for x in args) + '\n' + result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'Native reference command failed: {Path(args[0]).name}, exit {result.returncode}')
        return result.stdout

    loaded = mounted = False
    record = None
    try:
        version = invoke(commands['modinfo'], '-F', 'vermagic', module).strip()
        if not version or version.split()[0] != spec['kernel']:
            raise RuntimeError('Module vermagic does not match the native kernel')
        license_name = invoke(commands['modinfo'], '-F', 'license', module).strip()
        if license_name != 'GPL v2':
            raise RuntimeError('Unexpected native module license declaration')
        invoke(commands['cc'], '-std=c11', '-D_DEFAULT_SOURCE', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', ROOT / 'core/include', ROOT / 'fixtures/binder-device/check.c',
               ROOT / 'fixtures/binder-file/check.c',
               ROOT / 'fixtures/binder-mapping/check.c',
               ROOT / 'fixtures/binder-transaction/check.c', ROOT / 'core/src/binder_wire.c',
               ROOT / 'tests/native_binder_device.c', '-pthread', '-o', executable)
        invoke(commands['sudo'], '-n', commands['insmod'], module, 'devices=')
        loaded = True
        invoke(commands['sudo'], '-n', commands['mount'], '-t', 'binder', '-o', 'max=1', 'binder', mount)
        mounted = True
        output = invoke(commands['sudo'], '-n', executable, mount / 'binder-control', mount / 'artbox', timeout=20)
        result = json.loads(output)
        expected = len(re.findall(r'\bCHECK\(', (ROOT / 'fixtures/binder-device/check.c').read_text())) - 1
        file_cases = len(re.findall(r'\bCHECK\(', (ROOT / 'fixtures/binder-file/check.c').read_text())) - 1
        mapping_cases = len(re.findall(r'\bCHECK\(', (ROOT / 'fixtures/binder-mapping/check.c').read_text())) - 1
        if result != {'protocol': 8, 'cases': expected, 'file_cases': file_cases, 'mapping_cases': mapping_cases,
                      'same_pid_rejected': True, 'threaded_ping_pong': True,
                      'fresh_binderfs_context': True, 'passed': True}:
            raise RuntimeError('Native Binder reference did not execute every expected case')
        record = {**result, 'vermagic': version, 'module_license': license_name,
                  'runner_sha256': digest(executable)}
    finally:
        try:
            if mounted:
                invoke(commands['sudo'], '-n', commands['umount'], mount)
            if loaded:
                refcount = Path('/sys/module/binder_linux/refcnt')
                deadline = time.monotonic() + 5
                while int(refcount.read_text()) != 0 and time.monotonic() < deadline:
                    time.sleep(0.01)
                if int(refcount.read_text()) != 0:
                    raise RuntimeError('Binder device/mount references remain after private context cleanup')
                unload = json.loads(invoke(commands['sudo'], '-n', executable, '--probe-module-unload'))
                if unload != {'result': -1, 'errno': 16} or not Path('/sys/module/binder_linux').exists():
                    raise RuntimeError('Pinned module unload behavior differs from its missing exit hook')
                if record is not None:
                    record['cleanup'] = {'binderfs_unmounted': True, 'module_references': 0,
                                         'unload_probe': unload, 'module_remains_loaded_until_host_disposal': True}
        finally:
            (build / 'reference.log').write_text('\n'.join(logs), encoding='utf-8')
    return record


def compare_driver(build, reference):
    """Run the same fixture through the portable VM-backed endpoint boundary."""
    cmake = shutil.which('cmake')
    if cmake is None:
        raise RuntimeError('Driver comparison requires an existing CMake')
    build.mkdir(parents=True, exist_ok=True)
    log = build / 'comparison.log'
    with log.open('w', encoding='utf-8') as output:
        for command in (
            [cmake, '-S', str(ROOT), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'],
            [cmake, '--build', str(build), '--target', 'test_binder_device', 'test_binder_vfs',
             'test_binder_receive', '--parallel', '2'],
        ):
            output.write(' '.join(command) + '\n'); output.flush()
            subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=True, timeout=180)
        runner = build / 'test_binder_device'
        run = subprocess.run([str(runner)], capture_output=True, text=True, timeout=30)
        output.write(run.stdout + run.stderr)
        run.check_returncode()
        vfs_runner = build / 'test_binder_vfs'
        vfs_run = subprocess.run([str(vfs_runner)], capture_output=True, text=True, timeout=30)
        output.write(vfs_run.stdout + vfs_run.stderr)
        vfs_run.check_returncode()
        receive_runner = build / 'test_binder_receive'
        with tempfile.TemporaryDirectory(prefix='receive-', dir=build) as root:
            receive_run = subprocess.run([str(receive_runner), root], capture_output=True, text=True, timeout=30)
            output.write(receive_run.stdout + receive_run.stderr)
            receive_run.check_returncode()
            if any(Path(root).iterdir()):
                raise RuntimeError('Binder receive backing escaped its anonymous lifetime')
    result = json.loads(run.stdout)
    expected = {'protocol': reference['protocol'], 'shared_cases': reference['cases'],
                'vm_and_admission_controls': True, 'concurrent_lifecycles': 4000, 'passed': True}
    if result != expected:
        raise RuntimeError('Portable Binder driver did not pass the exact native fixture')
    vfs_result = json.loads(vfs_run.stdout)
    if vfs_result != {'shared_ioctl_cases': reference['cases'], 'shared_file_cases': reference['file_cases'],
                      'concurrent_lifecycles': 4000, 'passed': True}:
        raise RuntimeError('Binder VFS did not pass the exact native ioctl and descriptor fixtures')
    receive_result = json.loads(receive_run.stdout)
    if receive_result != dict(shared_mapping_cases=reference['mapping_cases'], native_alias_verified=True,
                              ownership_controls=True, passed=True):
        raise RuntimeError('Binder receive mapping did not pass the exact native lifetime fixture')
    return {**result, 'runner_sha256': digest(runner),
            'vfs': vfs_result, 'vfs_runner_sha256': digest(vfs_runner),
            'receive': receive_result, 'receive_runner_sha256': digest(receive_runner),
            'scope': 'Same ioctl, descriptor and mapping lifetime fixtures; no transaction, polling or death comparison'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-native', action='store_true')
    parser.add_argument('--disposable-host', action='store_true',
                        help='Acknowledge that the non-unloadable module remains until this test host is discarded')
    parser.add_argument('--compare-driver', action='store_true',
                        help='Build and run the same fixture through ARTBox after the native reference passes')
    parser.add_argument('--kernel', help='Package kernel to prepare; native execution must match it')
    args = parser.parse_args()
    if args.run_native and not args.disposable_host:
        parser.error('--run-native requires --disposable-host; the pinned module has no exit hook')
    if args.compare_driver and not args.run_native:
        parser.error('--compare-driver requires --run-native; preparation alone is not a reference result')
    os.environ.update(environment())
    pins = json.loads((ROOT / 'third_party/binder/kernel-reference.json').read_text())
    kernel = args.kernel or (platform.release() if args.run_native else pins['packages'][0]['kernel'])
    spec = next((p for p in pins['packages'] if p['kernel'] == kernel), None)
    if spec is None:
        raise RuntimeError('No reviewed module pin for this kernel; inspect the capability inventory and add its exact package')
    installed = prepare(spec, Path(os.environ['ARTBOX_CACHE_DIR']))
    record = {'scope': pins['scope'], 'package': spec, 'prepared': True,
              'native_execution_verified': False, 'artbox_driver_compared': False,
              'mapping_driver_compared': False, 'transaction_driver_compared': False}
    if args.run_native:
        record['native'] = run_native(spec, installed, Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/kernel-reference')
        record['native_execution_verified'] = True
        if args.compare_driver:
            record['driver'] = compare_driver(Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/kernel-reference/core', record['native'])
            record['artbox_driver_compared'] = True
            record['mapping_driver_compared'] = True
    record['project_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    sources = [
        ROOT / 'fixtures/binder-device/check.c', ROOT / 'fixtures/binder-device/check.h',
        ROOT / 'fixtures/binder-file/check.c', ROOT / 'fixtures/binder-file/check.h',
        ROOT / 'fixtures/binder-mapping/check.c', ROOT / 'fixtures/binder-mapping/check.h',
        ROOT / 'fixtures/binder-transaction/check.c', ROOT / 'fixtures/binder-transaction/check.h',
        ROOT / 'core/src/binder_wire.c',
        ROOT / 'tests/native_binder_device.c', ROOT / 'core/include/artbox/binder_wire.h',
        ROOT / 'third_party/binder/kernel-reference.json', Path(__file__).resolve()]
    if args.compare_driver:
        sources += [ROOT / 'CMakeLists.txt', ROOT / 'tests/test_binder_device.cpp', ROOT / 'tests/test_binder_vfs.cpp',
                    ROOT / 'tests/test_binder_receive.cpp']
        sources += [p for folder in ('core', 'platform') for p in (ROOT / folder).rglob('*')
                    if p.is_file() and p.suffix in ('.c', '.cpp', '.h', '.S')]
    record['project_files'] = {str(p.relative_to(ROOT)).replace('\\', '/'): digest(p) for p in sorted(set(sources))}
    (Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / 'm4-binder-kernel-reference.json').write_text(
        json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'prepared': True, 'kernel': kernel, 'native_execution_verified': record['native_execution_verified'],
                      'artbox_driver_compared': record['artbox_driver_compared'],
                      'mapping_driver_compared': record['mapping_driver_compared'],
                      'transaction_driver_compared': False,
                      'native': record.get('native'), 'driver': record.get('driver')}, indent=2))


if __name__ == '__main__':
    main()
