"""Build the real AOSP Looper caller; execute its Linux reference or emit Android objects."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import zipfile

from environment import ROOT, environment
from sources import obtain, obtain_files
from ndk import obtain as obtain_ndk
from build_binder import verify_object_architecture, symbol_inventory, ndk_notices
from icu_guest_link import check_code


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['linux', 'android'], required=True)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    args = parser.parse_args()
    if args.profile == 'linux' and (sys.platform != 'linux' or platform.machine().lower() not in
                                    ('aarch64', 'arm64', 'x86_64', 'amd64')):
        parser.error('The Linux oracle requires native ARM64 or x86_64 Linux')
    os.environ.update(environment())
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/looper' / args.profile).resolve()
    output.mkdir(parents=True, exist_ok=True)
    record_path = output / 'result.json'
    if record_path.exists(): record_path.unlink()
    commands = []

    def run(words, label, timeout=120):
        command = list(map(str, words))
        commands.append(command)
        process = subprocess.run(command, capture_output=True, timeout=timeout)
        (output / (label + '.stdout')).write_bytes(process.stdout)
        (output / (label + '.stderr')).write_bytes(process.stderr)
        if process.returncode:
            sys.stderr.buffer.write(process.stderr)
            raise RuntimeError('Looper command failed: ' + label)
        return process.stdout.decode('utf-8')

    graph = read(ROOT / 'third_party/binder/native-libraries.json')
    selection = read(ROOT / 'third_party/binder/native-sources.json')
    catalog = {name: selection[name] for name in ('binder-utils', 'binder-system', 'binder-build-reference')}
    sources = {name: obtain_files(name, spec, Path(os.environ['ARTBOX_CACHE_DIR'])) for name, spec in catalog.items()}
    common = read(ROOT / 'third_party/sources.json')
    for name in ('libbase-dex', 'liblog-dex', 'property-info'):
        catalog[name] = common[name]
        sources[name] = obtain(name)
    include_flags = [item for name in sources for path in graph['includes'].get(name, [])
                     for item in ('-I', str(sources[name] / path))]
    include_flags += ['-I', str(sources['property-info'] / 'libcutils/include')]
    flags = ['-std=c++20', '-O1', '-DNDEBUG', '-fPIC', '-fno-exceptions', '-fno-rtti',
             '-DANDROID_UTILS_CALLSTACK_ENABLED=0', '-DANDROID_UTILS_REF_BASE_DISABLE_IMPLICIT_CONSTRUCTION',
             '-DANDROID_BASE_UNIQUE_FD_DISABLE_IMPLICIT_CONVERSION',
             '-ffile-prefix-map=' + str(ROOT) + '=.', '-fdebug-prefix-map=' + str(ROOT) + '=.',
             '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
             '-Wno-c99-designator']
    cxx, toolchain = args.cxx, None
    notices = {}
    if args.profile == 'android':
        ndk = obtain_ndk(args.ndk_root)
        host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
        toolchain = ndk / 'toolchains/llvm/prebuilt' / host
        suffix = '.exe' if os.name == 'nt' else ''
        tool = lambda name: toolchain / 'bin' / (name + suffix)
        cxx = tool('clang++')
        flags += ['--target=aarch64-linux-android35', '-march=armv8-a', '-mno-outline-atomics',
                  '-ffixed-x18', '-ffixed-x27', '-ffixed-x28']
        notices.update(ndk_notices(ndk, toolchain))
    else:
        flags += ['-pthread']
    units = []
    for library in ('libutils_binder', 'libutils_looper'):
        spec = graph['libraries'][library]
        units += [(name, sources[spec['source']] / name, spec.get('unit_flags', {}).get(name, []))
                  for name in spec['units']]
    units.append(('check.cpp', ROOT / 'fixtures/looper/check.cpp', []))
    # The native host uses AOSP's actual host logging implementation. The Android
    # archive leaves log/C++ imports for the already-built signed dependencies.
    if args.profile == 'linux':
        for name in ('logger_name', 'logger_write', 'properties'):
            units.append(('log-' + name, sources['liblog-dex'] / 'liblog' / (name + '.cpp'),
                          ['-DLIBLOG_LOG_TAG=1006', '-DSNET_EVENT_LOG_TAG=1397638484', '-DANDROID_DEBUGGABLE=0']))
        units.append(('main.cpp', ROOT / 'tests/native_looper.cpp', []))
    objects, boundaries = {}, {}
    started = time.monotonic()
    for name, source, extra in units:
        obj = output / (name.replace('/', '__') + '.o')
        run([cxx, *flags, *include_flags, *extra, '-c', source, '-o', obj], obj.name)
        if args.profile == 'android':
            verify_object_architecture(obj)
            boundaries[obj.name] = check_code(run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', obj], obj.name + '-code'))
        objects[obj.name] = digest(obj)
    record = dict(profile=args.profile, commands=commands, objects=objects, boundaries=boundaries,
                  project_commit=run(['git', 'rev-parse', 'HEAD'], 'revision').strip(),
                  working_tree_dirty=bool(run(['git', 'status', '--porcelain'], 'worktree').strip()),
                  compiler_version=run([cxx, '--version'], 'compiler').strip(),
                  runtime_executed=False, guest_execution_verified=False, device_execution_verified=False)
    if args.profile == 'android':
        archive = output / 'libartbox_looper_check.a'
        if archive.exists(): archive.unlink()
        run([tool('llvm-ar'), 'rcsD', archive, *[output / n for n in objects]], 'archive')
        record['archive_sha256'] = digest(archive)
        record['symbols'] = symbol_inventory(run([tool('llvm-nm'), '--format=posix', '--extern-only', archive], 'symbols'))
    else:
        binary = output / 'native-looper'
        run([cxx, '-pthread', *[output / n for n in objects], '-o', binary], 'link')
        record['binary_sha256'] = digest(binary)
        record['native'] = json.loads(run([binary], 'native', timeout=15))
        expected = dict(status=0, cases=43, failure=0, wake_threads=1, message_calls=3,
                        fd_callbacks=1, timer_callbacks=1)
        if record['native'] != expected:
            raise RuntimeError('Incomplete real Looper reference: ' + repr(record['native']))
        record['negative_controls'] = {}
        for name, failure in (('retain-callback', 205), ('omit-wake', 109)):
            command = [str(binary), name]
            commands.append(command)
            process = subprocess.run(command, capture_output=True, timeout=15)
            (output / (name + '.stdout')).write_bytes(process.stdout)
            (output / (name + '.stderr')).write_bytes(process.stderr)
            result = json.loads(process.stdout)
            if process.returncode != 1 or result['failure'] != failure or result['status'] != -failure:
                raise RuntimeError('Looper negative control did not expose ' + name)
            record['negative_controls'][name] = result
        record['runtime_executed'] = True
    record['build_and_test_seconds'] = time.monotonic() - started
    files = {}
    for name, spec in catalog.items():
        for item in spec['files']:
            path = sources[name] / item['path']
            if digest(path) != item['sha256']: raise RuntimeError('Looper source changed: ' + item['path'])
            files['upstream/' + name + '/' + item['path']] = path
        notice = sources[name] / spec['notice']
        if digest(notice) != spec['notice_sha256']: raise RuntimeError('Looper notice changed: ' + name)
        notices[name + '.txt'] = notice
    for name, path in notices.items(): files['notices/' + name] = path
    project = ['LICENSE', 'THIRD_PARTY.md', 'docs/binder-build.md', 'docs/DECISIONS.md',
               'fixtures/looper/check.h', 'fixtures/looper/check.cpp', 'tests/native_looper.cpp',
               'scripts/test_aosp_looper.py', 'scripts/environment.py', 'scripts/sources.py',
               'scripts/ndk.py', 'scripts/build_binder.py', 'scripts/binder_aidl.py',
               'scripts/icu_guest_link.py', 'scripts/bionic_adapt.py', 'third_party/binder/aidl-tools.json',
               'third_party/binder/native-libraries.json', 'third_party/binder/native-sources.json',
               'third_party/bionic/builtins.json', 'third_party/sources.json', '.github/workflows/host-tests.yml']
    for name in project: files['artbox/' + name] = ROOT / name
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, path.read_bytes())
    record.update(sources=catalog, project_sources={name: digest(ROOT / name) for name in project},
                  source_bundle_sha256=digest(bundle), notices={name: digest(path) for name, path in notices.items()})
    record_path.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: record[key] for key in ('profile', 'runtime_executed', 'guest_execution_verified',
                                                  'build_and_test_seconds')}))


if __name__ == '__main__': main()
