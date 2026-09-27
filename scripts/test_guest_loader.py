"""Build AOSP libdl and exercise its signed Android API on native Apple ARM64."""
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
from sources import obtain
from ndk import obtain as obtain_ndk
from bionic_adapt import inventory
from dynamic_bundle import prepare
from test_loader_reference import EXPECTED


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/guest-loader'
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    output.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(words, name):
        command = list(map(str, words))
        result = subprocess.run(command, capture_output=True, timeout=120)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        commands.append(command)
        if result.returncode:
            raise RuntimeError('Guest loader failed; see ' + str(output / (name + '.log')))
        return result.stdout

    selection = json.loads((ROOT / 'third_party/bionic/libdl.json').read_text(encoding='utf-8'))
    pins = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    if selection['bionic_commit'] != pins['bionic']['commit']:
        raise RuntimeError('libdl selection differs from the pinned Bionic revision')
    bionic = obtain('bionic')
    for name, expected in selection['files'].items():
        if digest(bionic / name) != expected:
            raise RuntimeError('Pinned libdl source changed: ' + name)
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    tc = ndk / 'toolchains/llvm/prebuilt' / host
    tool = lambda name: tc / 'bin' / (name + ('.exe' if os.name == 'nt' else ''))
    common = ['--target=aarch64-linux-android35', '-O2', '-fPIC', '-fno-builtin', '-fno-stack-protector',
              '-march=armv8-a', '-mno-outline-atomics', '-mbranch-protection=none',
              '-ffixed-x18', '-ffixed-x27', '-ffixed-x28', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror']
    objects = {}
    for name, source in [('libdl', bionic / 'libdl/libdl.cpp'),
                         ('client', ROOT / 'fixtures/loader-api/check.c'),
                         ('provider', ROOT / 'fixtures/loader-api/provider.c')]:
        obj = output / (name + '.o')
        language = ['-std=c++17', '-fno-exceptions', '-fno-rtti'] if name == 'libdl' else ['-std=c11']
        run([tool('clang++' if name == 'libdl' else 'clang'), *common, *language,
             '-c', source, '-o', obj], name + '-compile')
        objects[name] = {'filename': obj.name, 'sha256': digest(obj), 'source_sha256': digest(source)}
    export_map = output / 'exports.map'
    export_map.write_text('{ global: ' + '; '.join(selection['exports']) + '; local: *; };\n', encoding='utf-8')
    flags = ['-shared', '--hash-style=both', '--build-id=none', '-z', 'max-page-size=16384',
             '--pack-dyn-relocs=relr', '-T', ROOT / 'fixtures/bionic-dynamic/image.ld']
    binaries = {'libdl': output / 'libdl.so', 'provider': output / 'libartbox_loader_provider.so',
                'client': output / 'libartbox_loader_client.so'}
    for name in ('libdl', 'provider', 'client'):
        extras = ['--gc-sections', '--version-script=' + str(export_map)] if name == 'libdl' else ['-z', 'defs']
        dependencies = ['--no-as-needed', binaries['libdl'], binaries['provider']] if name == 'client' else []
        run([tool('ld.lld'), *flags, *extras, '-soname', binaries[name].name,
             output / (name + '.o'), *dependencies, '-o', binaries[name]], name + '-link')
    records = {}
    expected_imports = {'libdl': sorted('__loader_' + n for n in selection['exports']), 'provider': [],
                        'client': sorted(n for n in selection['exports'] if n != 'dlvsym')}
    for name, elf in binaries.items():
        disassembly = run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf], name + '-disassembly').decode()
        boundary = inventory(disassembly)
        if not boundary['instruction_count'] or any(v for k, v in boundary.items() if k != 'instruction_count'):
            raise RuntimeError('Guest loader code violates the signed instruction boundary')
        imports = run([tool('llvm-nm'), '-D', '--undefined-only', '--format=posix', elf], name + '-imports').decode()
        if sorted(line.split()[0] for line in imports.splitlines()) != expected_imports[name]:
            raise RuntimeError('Unexpected guest loader imports: ' + name)
        if name == 'libdl':
            exports = run([tool('llvm-nm'), '-D', '--defined-only', '--format=posix', elf], 'libdl-exports').decode()
            if sorted(line.split()[0] for line in exports.splitlines()) != sorted(selection['exports']):
                raise RuntimeError('Unexpected AOSP frontend exports')
        records[name] = {'filename': elf.name, 'sha256': digest(elf), 'bytes': elf.stat().st_size,
                         'inventory': boundary, 'imports': expected_imports[name]}
        run([sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', elf, output / (name + '-pack')], name + '-pack')
    ndk_notice = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
    notices = {'BIONIC-LIBDL-NOTICE.txt': (bionic / 'libdl/NOTICE', selection['files']['libdl/NOTICE']),
               'NDK-NOTICE.txt': (tc / 'NOTICE', ndk_notice), 'ARTBOX-LICENSE.txt': (ROOT / 'LICENSE', digest(ROOT / 'LICENSE'))}
    for path, expected in notices.values():
        if digest(path) != expected: raise RuntimeError('Guest loader notice changed: ' + str(path))
    project = ['scripts/test_guest_loader.py', 'scripts/test_loader_reference.py', 'scripts/environment.py',
               'scripts/sources.py', 'scripts/ndk.py', 'scripts/bionic_adapt.py', 'scripts/dynamic_bundle.py',
               'scripts/guest_bundle.py', 'tools/wrap_dynamic.py', 'tools/pack_elf.py',
               'third_party/bionic/libdl.json', 'third_party/bionic/builtins.json', 'third_party/sources.json',
               'fixtures/bionic-dynamic/image.ld', 'fixtures/loader-api/check.c', 'fixtures/loader-api/provider.c',
               'tests/native_loader.c', 'CMakeLists.txt', '.github/workflows/host-tests.yml', 'LICENSE', 'THIRD_PARTY.md']
    project += [p.relative_to(ROOT).as_posix() for directory in ('core', 'platform')
                for p in (ROOT / directory).rglob('*') if p.is_file()]
    project = sorted(set(project))
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name in selection['files']: z.write(bionic / name, 'upstream/bionic/' + name)
        for name, (path, _) in notices.items(): z.write(path, 'notices/' + name)
        for name in project: z.write(ROOT / name, 'artbox/' + name)
    report = {'project_commit': run(['git', 'rev-parse', 'HEAD'], 'revision').decode().strip(),
              'scope': 'Fixed signed startup group and original AOSP libdl frontend; not ART startup',
              'selection': selection, 'objects': objects, 'binaries': records, 'commands': commands,
              'project_sources': {p: digest(ROOT / p) for p in project},
              'source_bundle_sha256': digest(bundle), 'notices': {n: h for n, (_, h) in notices.items()},
              'frameworks': {}, 'device_execution_verified': False}

    def save():
        (artifacts / 'm3-guest-loader.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    save()
    if sys.platform == 'darwin':
        if platform.machine().lower() not in ('arm64', 'aarch64'):
            raise RuntimeError('Signed loader execution requires native ARM64 macOS')
        for target in ('macos', 'ios'):
            frameworks = {}
            for name in ('client', 'libdl', 'provider'):
                binary, details = prepare(output / (name + '-pack'), output / target / name, target,
                    bionic / 'libdl/NOTICE', selection['files']['libdl/NOTICE'],
                    {n: pair for n, pair in notices.items() if n != 'BIONIC-LIBDL-NOTICE.txt'},
                    name='ARTBoxLoader' + name.title(), notice_name='BIONIC-LIBDL-NOTICE.txt')
                frameworks[name] = binary
                report['frameworks'][target + '-' + name] = details
                save()
            if target == 'macos':
                runner = Path(os.environ['ARTBOX_BUILD_DIR']) / 'host/artbox_native_loader'
                arguments = [p for name in ('client', 'libdl', 'provider') for p in (frameworks[name], binaries[name])]
                started = time.perf_counter_ns()
                observed = json.loads(run([runner, *arguments], 'native'))
                report['process_ns'] = time.perf_counter_ns() - started
                if observed != EXPECTED:
                    raise RuntimeError('Signed Android loader and native Linux reference disagree')
                report['native'] = observed
                save()
    print('AOSP libdl frontend and original Android loader fixture built; ' +
          ('32 signed Mac cases, six thread-error checks and iOS 15 packaging pass' if sys.platform == 'darwin'
           else 'native execution pending'))


if __name__ == '__main__':
    main()
