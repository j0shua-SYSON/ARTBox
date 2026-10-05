"""Link real AOSP Looper into signed Apple code and run its shared Bionic contract."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile
import zipfile
from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from build_binder import verify_object_architecture
from dynamic_bundle import prepare
from icu_guest_link import check_code
from looper_guest import EXPECTED, verify_result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', required=True, type=Path)
    parser.add_argument('--guest-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('Signed native Looper execution requires an ARM64 Mac')
    os.environ.update(environment())
    builds, artifacts = [Path(os.environ[n]) for n in ('ARTBOX_BUILD_DIR', 'ARTBOX_ARTIFACTS_DIR')]
    output = (args.build_dir or builds / 'm4/looper-guest').resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(words, label):
        command = list(map(str, words))
        commands.append(command)
        process = subprocess.run(command, capture_output=True, timeout=120)
        (output / (label + '.stdout')).write_bytes(process.stdout)
        (output / (label + '.stderr')).write_bytes(process.stderr)
        if process.returncode:
            sys.stderr.buffer.write(process.stderr)
            raise RuntimeError('Looper packaging failed: ' + label)
        return process.stdout.decode('utf-8')

    def verify(path, expected):
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError('Looper input differs from its producer: ' + str(path))
        return path

    revision = run(['git', 'rev-parse', 'HEAD'], 'revision').strip()
    if run(['git', 'status', '--porcelain'], 'worktree').strip():
        raise RuntimeError('Signed Looper requires a clean producer revision')
    native, guest, deps = [p.resolve() for p in (args.native_dir, args.guest_dir, args.dependency_dir)]
    build = read(native / 'result.json')
    art = read(artifacts / 'm3-art-guest-link.json')
    bionic = read(deps / 'artifacts/m2-bionic-startup.json')
    math = read(deps / 'artifacts/m3-art-math.json')
    loader = read(deps / 'artifacts/m3-guest-loader.json')
    for record in (build, art, bionic, math, loader):
        if record['project_commit'] != revision:
            raise RuntimeError('Mixed Looper dependency producer revisions')
        for name, expected in record.get('project_sources', {}).items():
            verify(ROOT / name, expected)
    if (build['profile'] != 'android' or build['working_tree_dirty'] or build['runtime_executed'] or
            art['input_revision'] != revision or art['working_tree_dirty']):
        raise RuntimeError('Require Android Looper objects and the current clean ART closure')
    bundles = {'looper': verify(native / 'corresponding-source.zip', build['source_bundle_sha256']),
               'art': verify(guest / 'corresponding-source.zip', art['source_bundle_sha256'])}
    graph = read(ROOT / 'third_party/binder/native-libraries.json')['libraries']
    names = [n.replace('/', '__') + '.o' for lib in ('libutils_binder', 'libutils_looper') for n in graph[lib]['units']]
    names.append('check.cpp.o')
    if set(names) != set(build['objects']) or len(names) != 11:
        raise RuntimeError('Looper caller/source selection changed')
    objects = [verify(native / n, build['objects'][n]) for n in names]
    for obj in objects: verify_object_architecture(obj)
    modules = [
        (deps / 'build/m2/bionic-startup/libc/macos/ARTBoxBionic.framework/ARTBoxBionic',
         deps / 'build/m2/bionic-startup/libc.so', art['dependencies']['libc.so'],
         bionic['images']['libc']['frameworks']['macos']),
        (guest / 'macos/ARTBoxRuntime.framework/ARTBoxRuntime', guest / 'libart.so',
         art['elf_sha256'], art['frameworks']['macos']),
        (deps / 'build/m3/art-math/macos/library/ARTBoxMath.framework/ARTBoxMath',
         deps / 'build/m3/art-math/libm.so', art['dependencies']['libm.so'], math['frameworks']['macos-library']),
        (deps / 'build/m3/guest-loader/macos/libdl/ARTBoxLoaderLibdl.framework/ARTBoxLoaderLibdl',
         deps / 'build/m3/guest-loader/libdl.so', art['dependencies']['libdl.so'], loader['frameworks']['macos-libdl'])]
    for binary, elf, expected, details in modules:
        verify(elf, expected)
        verify(binary, details['layout']['macho_sha256'])
        if not details['signature_verified'] or details['entitlements']:
            raise RuntimeError('Looper dependencies require ordinary signed frameworks')
    ndk = obtain_ndk(args.ndk_root)
    tc = ndk / 'toolchains/llvm/prebuilt/darwin-x86_64'
    tool = lambda name: tc / 'bin' / name
    crt = tc / 'sysroot/usr/lib/aarch64-linux-android/35'
    linker = output / 'image.ld'
    script = (ROOT / 'fixtures/bionic-dynamic/image.ld').read_text(encoding='utf-8')
    script = script.replace('KEEP(*(.init_array .init_array.*))',
                            'KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))')
    script = script.replace('*(COMMON)', '*(COMMON) . = ALIGN(16384);')
    script = script.replace('    .bss (NOLOAD)', '    .tdata : { *(.tdata .tdata.*) } :writable\n'
                            '    .tbss (NOLOAD) : { *(.tbss .tbss.*) } :writable\n    .bss (NOLOAD)')
    script += '\nASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0, "Looper TLS requires an explicit integration")\n'
    linker.write_text(script, encoding='utf-8')
    exports = output / 'exports.map'
    exports.write_text('{ global: artbox_native_looper_check; local: *; };\n', encoding='utf-8')
    elf = output / 'libartbox_looper_check.so'
    run([tool('ld.lld'), '-shared', '-z', 'defs', '--hash-style=both', '--build-id=none',
         '-z', 'max-page-size=16384', '--pack-dyn-relocs=relr', '--no-relax', '-T', linker,
         '--version-script=' + str(exports), '-soname', elf.name, crt / 'crtbegin_so.o',
         *objects, '--no-as-needed', *[row[1] for row in modules], crt / 'crtend_so.o', '-o', elf], 'link')
    dynamic = run([tool('llvm-readelf'), '--dynamic', elf], 'dynamic')
    needed = re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]', dynamic)
    if needed != [row[1].name for row in modules]:
        raise RuntimeError('Looper root must retain exactly its four bootstrap dependencies')
    symbols = run([tool('llvm-nm'), '-D', '--defined-only', '--format=posix', elf], 'exports')
    if {row.split()[0] for row in symbols.splitlines()} != {'artbox_native_looper_check'}:
        raise RuntimeError('Looper support symbols escaped their image-local scope')
    boundary = check_code(run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf], 'instructions'))
    packed = output / 'pack'
    run([sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', elf, packed], 'pack')
    notices = {}
    with zipfile.ZipFile(bundles['looper']) as source:
        for name, expected in build['notices'].items():
            if Path(name).name != name: raise RuntimeError('Unexpected Looper notice name')
            path = output / 'notices' / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(source.read('notices/' + name))
            notices[name] = (verify(path, expected), expected)
    notices['ARTBOX-LICENSE.txt'] = (ROOT / 'LICENSE', digest(ROOT / 'LICENSE'))
    primary = notices.pop('binder-utils.txt')
    frameworks = {}
    for target in ('macos', 'ios'):
        _, frameworks[target] = prepare(packed, output / target, target, *primary, notices,
                                        name='ARTBoxLooper', notice_name='LIBUTILS-NOTICE.txt')
    modules.append((output / 'macos/ARTBoxLooper.framework/ARTBoxLooper', elf, digest(elf), frameworks['macos']))
    runner = builds / 'host/artbox_native_looper'
    project = run(['git', 'ls-files', 'core', 'platform', 'CMakeLists.txt', 'LICENSE', 'THIRD_PARTY.md',
                   'docs/binder-build.md', 'docs/DECISIONS.md', 'fixtures/looper', 'fixtures/bionic-dynamic/image.ld',
                   'scripts/test_looper_guest.py', 'scripts/looper_guest.py', 'scripts/test_aosp_looper.py',
                   'scripts/dynamic_bundle.py', 'scripts/guest_bundle.py', 'scripts/icu_guest_link.py',
                   'scripts/bionic_adapt.py', 'scripts/build_binder.py', 'scripts/ndk.py', 'scripts/environment.py',
                   'tools/wrap_dynamic.py', 'tests/native_looper_guest.c', 'tests/test_looper_guest.py',
                   'third_party/bionic/m2-objects.json', '.github/workflows/host-tests.yml'], 'sources').splitlines()
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in bundles.items(): archive.write(path, 'inputs/' + name + '-source.zip')
        for name, record in [('build', build), ('art', art), ('bionic', bionic), ('math', math), ('loader', loader)]:
            archive.writestr('inputs/' + name + '.json', json.dumps(record, indent=2))
        for name in project: archive.write(ROOT / name, 'artbox/' + name)
        archive.write(linker, 'generated/image.ld')
        archive.write(exports, 'generated/exports.map')
    record = dict(project_commit=revision, working_tree_dirty=False, commands=commands,
                  scope='Real AOSP Looper through signed Bionic; no servicemanager or JavaVM startup',
                  project_sources={name: digest(ROOT / name) for name in project},
                  source_bundle_sha256=digest(bundle), runner_sha256=digest(runner),
                  elf_sha256=digest(elf), elf_bytes=elf.stat().st_size, needed=needed, boundary=boundary,
                  frameworks=frameworks, crt_objects={p.name:digest(p) for p in (crt/'crtbegin_so.o', crt/'crtend_so.o')},
                  inputs={row[1].name:dict(elf_sha256=row[2], framework_sha256=digest(row[0])) for row in modules},
                  executions={}, guest_execution_verified=False, device_execution_verified=False)
    report = artifacts / 'm4-looper-guest.json'
    try:
        for name in EXPECTED:
            root = Path(tempfile.mkdtemp(prefix=name + '-', dir=output))
            for folder in ('data', 'system'): (root / folder).mkdir()
            command = [str(runner), *[str(p) for row in modules for p in row[:2]], str(root)]
            if name != 'normal': command.append(name)
            commands.append(command)
            try:
                process = subprocess.run(command, capture_output=True, timeout=30)
            except subprocess.TimeoutExpired as error:
                (output / (name + '.stdout')).write_bytes(error.stdout or b'')
                (output / (name + '.stderr')).write_bytes(error.stderr or b'')
                record['executions'][name] = dict(timeout=True)
                raise
            (output / (name + '.stdout')).write_bytes(process.stdout)
            (output / (name + '.stderr')).write_bytes(process.stderr)
            observed = dict(exit=process.returncode, stdout_sha256=hashlib.sha256(process.stdout).hexdigest(),
                            stderr_sha256=hashlib.sha256(process.stderr).hexdigest())
            record['executions'][name] = observed
            try:
                observed['native'] = json.loads(process.stdout)
                verify_result(name, process.returncode, observed['native'])
            except (ValueError, TypeError):
                sys.stderr.buffer.write(process.stderr)
                raise
        record['guest_execution_verified'] = True
    finally:
        report.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Signed Bionic executes the real AOSP Looper: 43 shared cases and both negative controls pass')


if __name__ == '__main__': main()
