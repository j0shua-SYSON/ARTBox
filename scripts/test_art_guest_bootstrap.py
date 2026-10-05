"""Execute signed ART constructors, real sigchain and the pre-start JNI/heap boundary."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
from environment import ROOT, environment


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--guest-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Signed native ARM64 execution requires macOS')
    os.environ.update(environment())
    builds = Path(os.environ['ARTBOX_BUILD_DIR'])
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    output = builds / 'm3/art-guest-bootstrap'
    output.mkdir(parents=True, exist_ok=True)
    read = lambda p: json.loads(p.read_text(encoding='utf-8'))
    linked = read(artifacts / 'm3-art-guest-link.json')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if linked['project_commit'] != revision or linked['input_revision'] != revision or linked['working_tree_dirty']:
        raise RuntimeError('Require signed ART from the current clean producer revision')
    deps, guest = args.dependency_dir, args.guest_dir
    bionic = read(deps / 'artifacts/m2-bionic-startup.json')
    math = read(deps / 'artifacts/m3-art-math.json')
    loader = read(deps / 'artifacts/m3-guest-loader.json')
    if any(r['project_commit'] != revision for r in (bionic, math, loader)):
        raise RuntimeError('Bootstrap dependency revision changed')
    modules = [
        (deps / 'build/m2/bionic-startup/libc/macos/ARTBoxBionic.framework/ARTBoxBionic',
         deps / 'build/m2/bionic-startup/libc.so', linked['dependencies']['libc.so'],
         bionic['images']['libc']['frameworks']['macos']),
        (guest / 'macos/ARTBoxRuntime.framework/ARTBoxRuntime', guest / 'libart.so',
         linked['elf_sha256'], linked['frameworks']['macos']),
        (deps / 'build/m3/art-math/macos/library/ARTBoxMath.framework/ARTBoxMath',
         deps / 'build/m3/art-math/libm.so', linked['dependencies']['libm.so'], math['frameworks']['macos-library']),
        (deps / 'build/m3/guest-loader/macos/libdl/ARTBoxLoaderLibdl.framework/ARTBoxLoaderLibdl',
         deps / 'build/m3/guest-loader/libdl.so', linked['dependencies']['libdl.so'], loader['frameworks']['macos-libdl'])]
    for binary, elf, expected, framework in modules:
        if digest(elf) != expected or digest(binary) != framework['layout']['macho_sha256']:
            raise RuntimeError('Bootstrap executable input changed: ' + str(elf))
        if not framework['signature_verified'] or framework['entitlements']:
            raise RuntimeError('Bootstrap requires verified ordinary signed frameworks')
    root = Path(tempfile.mkdtemp(prefix='root-', dir=output))
    for name in ('data', 'system'): (root / name).mkdir()
    runner = builds / 'host/artbox_native_art_bootstrap'
    command = [str(runner), *[str(p.resolve()) for row in modules for p in row[:2]], str(root.resolve())]
    paths = subprocess.check_output(['git', 'ls-files', 'core', 'platform', 'CMakeLists.txt',
        'scripts/test_art_guest_bootstrap.py', 'tests/native_art_bootstrap.c', 'LICENSE'], text=True).splitlines()
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for p in paths: z.write(ROOT / p, 'artbox/' + p)
    result = {'project_commit': revision, 'scope': 'Signed guest constructors, real sigchain, pre-start JNI and shared heap',
              'project_sources': {p: digest(ROOT / p) for p in paths}, 'source_bundle_sha256': digest(bundle),
              'runner_sha256': digest(runner), 'command': command, 'elf_sha256': linked['elf_sha256'],
              'framework_sha256': {elf.name: digest(binary) for binary, elf, _, _ in modules},
              'runtime_started': False, 'dex_executed': False, 'device_execution_verified': False}
    try:
        process = subprocess.run(command, capture_output=True, timeout=60)
        (output / 'native.stdout').write_bytes(process.stdout)
        (output / 'native.stderr').write_bytes(process.stderr)
        result['exit'] = process.returncode
        if process.returncode:
            print(process.stderr.decode('utf-8', errors='replace'), file=sys.stderr)
        process.check_returncode()
        native = json.loads(process.stdout)
        result['native'] = native
        if (native['constructors'] < 1 or native['tls_modules'] < 1 or native['linked_images'] != 4 or
                native['registered_vms'] or not native['heap_binding_verified'] or not native['cleanup'] or
                native['runtime_started'] or native['dex_executed'] or
                native['vm_budget_bytes'] != 1536 << 20 or
                not 0 < native['reserved_bytes'] <= 1536 << 20 or
                native['bootstrap_window_bytes'] != 4 * os.sysconf('SC_PAGE_SIZE') or
                native['sigchain_cases'] != 22 or native['sigchain_mutation'] != -1005):
            raise RuntimeError('Incomplete signed ART bootstrap contract')
    except subprocess.TimeoutExpired as error:
        (output / 'native.stdout').write_bytes(error.stdout or b'')
        (output / 'native.stderr').write_bytes(error.stderr or b'')
        result['timeout'] = True
        raise
    finally:
        (artifacts / 'm3-art-guest-bootstrap.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Signed ART constructors, real sigchain, pre-start JNI and shared heap verified; JavaVM/DEX pending')


if __name__ == '__main__': main()
