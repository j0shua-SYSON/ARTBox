"""Execute the original ICU checks through signed Android libraries and shared Bionic services."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
from environment import ROOT, environment
from link_icu_guest import LIBRARIES


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--icu-dir', required=True, type=Path)
    parser.add_argument('--guest-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin': parser.error('Signed native ARM64 execution requires macOS')
    os.environ.update(environment())
    builds, artifacts = [Path(os.environ[name]) for name in ('ARTBOX_BUILD_DIR', 'ARTBOX_ARTIFACTS_DIR')]
    output = builds / 'm3/icu-guest'
    output.mkdir(parents=True, exist_ok=True)
    read = lambda p: json.loads(p.read_text(encoding='utf-8'))
    icu, guest, deps = args.icu_dir, args.guest_dir, args.dependency_dir
    linked = read(artifacts / 'm3-icu-guest-link.json')
    art = read(artifacts / 'm3-art-guest-link.json')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    for report in (linked, art):
        if report['project_commit'] != revision or report['input_revision'] != revision or report['working_tree_dirty']:
            raise RuntimeError('Require ICU and ART from the current clean producer revision')
    bionic = read(deps / 'artifacts/m2-bionic-startup.json')
    math = read(deps / 'artifacts/m3-art-math.json')
    loader = read(deps / 'artifacts/m3-guest-loader.json')
    if any(r['project_commit'] != revision for r in (bionic, math, loader)):
        raise RuntimeError('ICU execution dependency revision changed')
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
    for _, elf, expected, _ in modules:
        if linked['base_inputs'][elf.name] != expected: raise RuntimeError('ICU base dependency changed')
    order = [name for name, _, _, _ in LIBRARIES] + ['libartbox_icu_check.so']
    if set(linked['libraries']) != set(order): raise RuntimeError('Unexpected ICU library set')
    for name in order:
        details = linked['libraries'][name]
        framework = details['framework_name']
        modules.append((icu / 'macos' / (framework + '.framework') / framework,
                        icu / name, details['elf_sha256'], details['frameworks']['macos']))
    for binary, elf, expected, framework in modules:
        if digest(elf) != expected or digest(binary) != framework['layout']['macho_sha256']:
            raise RuntimeError('ICU executable input changed: ' + str(elf))
        if not framework['signature_verified'] or framework['entitlements']:
            raise RuntimeError('ICU requires verified ordinary signed frameworks')
    data = icu / 'i18n/etc/icu/icudt75l.dat'
    if digest(data) != linked['data']['sha256']: raise RuntimeError('ICU data changed')
    root = Path(tempfile.mkdtemp(prefix='root-', dir=output))
    for directory in ('data', 'system/i18n/etc/icu', 'system/tzdata'):
        (root / directory).mkdir(parents=True, exist_ok=True)
    shutil.copyfile(data, root / 'system/i18n/etc/icu/icudt75l.dat')
    runner = builds / 'host/artbox_native_icu'
    command = [str(runner), *[str(p.resolve()) for row in modules for p in row[:2]], str(root.resolve())]
    paths = subprocess.check_output(['git', 'ls-files', 'core', 'platform', 'CMakeLists.txt',
        'scripts/test_icu_guest.py', 'tests/native_icu.c', 'fixtures/art-runtime/native_icu.cpp', 'LICENSE'],
        text=True).splitlines()
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in paths: archive.write(ROOT / path, 'artbox/' + path)
    result = {'project_commit': revision, 'scope': 'Signed ICU native checks through Bionic; no JavaVM/DEX',
              'project_sources': {p: digest(ROOT / p) for p in paths}, 'source_bundle_sha256': digest(bundle),
              'runner_sha256': digest(runner), 'command': command,
              'elf_sha256': {elf.name: expected for _, elf, expected, _ in modules},
              'framework_sha256': {elf.name: digest(binary) for binary, elf, _, _ in modules},
              'data_sha256': digest(data), 'runtime_started': False, 'dex_executed': False,
              'jni_onload_invoked': False, 'device_execution_verified': False}
    try:
        process = subprocess.run(command, capture_output=True, timeout=60)
        (output / 'native.stdout').write_bytes(process.stdout)
        (output / 'native.stderr').write_bytes(process.stderr)
        result['exit'] = process.returncode
        if process.returncode: sys.stderr.buffer.write(process.stderr)
        process.check_returncode()
        native = json.loads(process.stdout)
        result['native'] = native
        if (native['icu_cases'] != 8 or native['icu_check_ns'] <= 0 or native['constructors'] < 31 or native['tls_modules'] < 1 or
                native['linked_images'] != 10 or native['registered_vms'] or
                not native['heap_binding_verified'] or not native['cleanup'] or
                native['runtime_started'] or native['dex_executed']):
            raise RuntimeError('Incomplete signed ICU execution contract')
    except subprocess.TimeoutExpired as error:
        (output / 'native.stdout').write_bytes(error.stdout or b'')
        (output / 'native.stderr').write_bytes(error.stderr or b'')
        result['timeout'] = True
        raise
    finally:
        (artifacts / 'm3-icu-guest.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Eight native ICU groups pass through signed Android libraries; JavaVM startup and DEX pending')


if __name__ == '__main__': main()
