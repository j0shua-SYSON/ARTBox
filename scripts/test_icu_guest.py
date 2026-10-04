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
from link_libcore_guest import LIBRARIES as LIBCORE_LIBRARIES


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runtime_progress(stderr):
    """Record observed phases even when a later operation fails or times out."""
    lines = stderr.decode('utf-8', errors='replace').splitlines()
    return {
        'runtime_invocation_attempted': 'ARTBox: entering signed ART JNI_CreateJavaVM' in lines,
        'runtime_started': 'ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache' in lines,
        'dex_executed': 'ARTBox: signed ART method returned the expected string' in lines,
        'lifecycle_verified': 'ARTBox: signed ART lifecycle checks passed' in lines,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--icu-dir', required=True, type=Path)
    parser.add_argument('--guest-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    parser.add_argument('--libcore-dir', type=Path, help='Also execute the signed native class-library checks')
    parser.add_argument('--classlib-dir', type=Path, help='Start ART using this verified implementation DEX bundle')
    parser.add_argument('--managed-fixture-dir', type=Path)
    args = parser.parse_args()
    runtime = args.classlib_dir is not None
    if runtime != (args.managed_fixture_dir is not None) or (runtime and not args.libcore_dir):
        parser.error('Runtime execution requires libcore, classlib and managed-fixture directories together')
    if sys.platform != 'darwin': parser.error('Signed native ARM64 execution requires macOS')
    os.environ.update(environment())
    builds, artifacts = [Path(os.environ[name]) for name in ('ARTBOX_BUILD_DIR', 'ARTBOX_ARTIFACTS_DIR')]
    label = 'art-runtime' if runtime else 'libcore' if args.libcore_dir else 'icu'
    output = builds / ('m3/' + label + '-guest')
    output.mkdir(parents=True, exist_ok=True)
    read = lambda p: json.loads(p.read_text(encoding='utf-8'))
    icu, guest, deps = args.icu_dir, args.guest_dir, args.dependency_dir
    linked = read(artifacts / 'm3-icu-guest-link.json')
    art = read(artifacts / 'm3-art-guest-link.json')
    libcore = read(artifacts / 'm3-libcore-guest-link.json') if args.libcore_dir else None
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    for report in [linked, art, *([libcore] if libcore else [])]:
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
    order = [name for name, _, _, _ in LIBRARIES]
    if set(linked['libraries']) != set(order + ['libartbox_icu_check.so']): raise RuntimeError('Unexpected ICU library set')
    if not libcore: order.append('libartbox_icu_check.so')
    for name in order:
        details = linked['libraries'][name]
        framework = details['framework_name']
        modules.append((icu / 'macos' / (framework + '.framework') / framework,
                        icu / name, details['elf_sha256'], details['frameworks']['macos']))
    if libcore:
        for _, elf, expected, _ in modules:
            if libcore['base_inputs'][elf.name] != expected: raise RuntimeError('Libcore base dependency changed')
        order = [name for name, _, _, _, _ in LIBCORE_LIBRARIES] + ['libartbox_libcore_check.so']
        if set(libcore['libraries']) != set(order): raise RuntimeError('Unexpected libcore library set')
        for name in order:
            details = libcore['libraries'][name]
            framework = details['framework_name']
            modules.append((args.libcore_dir / 'macos' / (framework + '.framework') / framework,
                            args.libcore_dir / name, details['elf_sha256'], details['frameworks']['macos']))
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
    dex_inputs = {}
    if runtime:
        from test_art_startup import prepare_classlib
        from build_art_managed_fixture import prepare_fixture
        from dex_fixture import make_hello
        for name in ('system/framework', 'system/art', 'system_ext', 'data/scratch'):
            (root / name).mkdir(parents=True, exist_ok=True)
        boot, boot_report = prepare_classlib(args.classlib_dir, output)
        managed, managed_report = prepare_fixture(args.managed_fixture_dir, output)
        if boot_report['project_commit'] != revision or managed_report['project_commit'] != revision:
            raise RuntimeError('Runtime DEX producers differ from the signed runtime revision')
        for path in boot: shutil.copyfile(path, root / 'system/framework' / path.name)
        shutil.copyfile(managed, root / 'data/runtime-checks.dex')
        hello = root / 'data/hello.dex'
        hello.write_bytes(make_hello()[0])
        dex_inputs = {'boot_dex': boot_report['dex_files'], 'managed_fixture': managed_report,
                      'hello_sha256': digest(hello)}
    runner = builds / ('host/artbox_native_' + label.replace('-', '_'))
    command = [str(runner), *[str(p.resolve()) for row in modules for p in row[:2]], str(root.resolve())]
    paths = subprocess.check_output(['git', 'ls-files', 'core', 'platform', 'CMakeLists.txt',
        'scripts/test_icu_guest.py', 'tests/native_icu.c', 'fixtures/art-runtime/native_icu.cpp',
        'tests/native_libcore.c', 'fixtures/art-runtime/native_libcore.cpp', 'fixtures/libcore-integer128/check.c',
        'tests/native_art_runtime.c', 'fixtures/art-runtime/native_runtime.cpp', 'fixtures/art-runtime/managed_checks.cpp',
        'scripts/test_art_startup.py', 'scripts/build_art_managed_fixture.py', 'scripts/dex_fixture.py', 'LICENSE'],
        text=True).splitlines()
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in paths: archive.write(ROOT / path, 'artbox/' + path)
    result = {'project_commit': revision, 'scope': 'Signed ' + label + ' native checks through Bionic; no JavaVM/DEX',
              'project_sources': {p: digest(ROOT / p) for p in paths}, 'source_bundle_sha256': digest(bundle),
              'runner_sha256': digest(runner), 'command': command,
              'elf_sha256': {elf.name: expected for _, elf, expected, _ in modules},
              'framework_sha256': {elf.name: digest(binary) for binary, elf, _, _ in modules},
              'data_sha256': digest(data), 'runtime_started': False, 'dex_executed': False,
              'jni_onload_invoked': False, 'device_execution_verified': False, 'passed': False}
    if runtime:
        result.update(scope='Signed ART JavaVM, hello DEX, managed checks and VM shutdown', **dex_inputs)
        result.update(runtime_progress(b''))
        result.pop('jni_onload_invoked')  # No separate JNI_OnLoad call-count observation.
    process_label = 'native'
    try:
        process = subprocess.run(command, capture_output=True, timeout=60)
        (output / 'native.stdout').write_bytes(process.stdout)
        (output / 'native.stderr').write_bytes(process.stderr)
        result['exit'] = process.returncode
        if runtime: result.update(runtime_progress(process.stderr))
        if process.returncode: sys.stderr.buffer.write(process.stderr)
        process.check_returncode()
        native = json.loads(process.stdout)
        result['native'] = native
        if (native['constructors'] < 35 or native['tls_modules'] < 1 or
                native['linked_images'] != (15 if libcore else 10) or native['registered_vms'] or
                not native['heap_binding_verified'] or not native['cleanup'] or
                native['runtime_started'] != runtime or native['dex_executed'] != runtime):
            raise RuntimeError('Incomplete signed native dependency execution contract')
        if runtime:
            stderr = process.stderr.decode('utf-8', errors='replace')
            for message in ('ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache',
                            'hello from ARTBox ART', 'ARTBox: signed ART method returned the expected string',
                            'ARTBox: signed ART lifecycle checks passed'):
                if message not in stderr.splitlines(): raise RuntimeError('Missing signed ART observation: ' + message)
            def observation(prefix):
                lines = [line[len(prefix):] for line in stderr.splitlines() if line.startswith(prefix)]
                if len(lines) != 1: raise RuntimeError('Missing or duplicate runtime observation: ' + prefix)
                return json.loads(lines[0])
            managed = observation('ARTBox managed checks: ')
            threads = observation('ARTBox thread state: ')
            worker = observation('ARTBox VM worker: ')
            if (worker.get('primordial') is not False or worker.get('current_in_stack') is not True or
                    worker.get('requested_stack_bytes') != 4 * 1024 * 1024 or
                    type(worker.get('reported_stack_bytes')) is not int or
                    type(worker.get('guard_bytes')) is not int or
                    not (0 <= worker['guard_bytes'] < worker['reported_stack_bytes'])):
                raise RuntimeError('Signed ART did not run on the checked Bionic worker stack')
            if (managed.get('heap_checksum') != 6496 or managed.get('exceptions') != 3 or
                    managed.get('attachments') != 4 or
                    not (type(managed.get('gc_before')) is int and type(managed.get('gc_after')) is int and
                         0 <= managed['gc_before'] < managed['gc_after']) or
                    threads != {'threads': 3, 'attach_cycles': 4, 'tls_isolated': True, 'main_tls_restored': True} or
                    any(type(native.get(key)) is not int or native[key] <= 0 for key in
                        ('startup_ns', 'managed_bytes', 'process_peak_rss_bytes', 'threads_reaped'))):
                raise RuntimeError('Signed ART managed, thread or memory contract failed')
            result.update(runtime_started=True, dex_executed=True, managed_checks=managed,
                          thread_state_checks=threads, vm_worker=worker, lifecycle_verified=True)
            # A second process must reach the real VM and fail its missing-class lookup.
            hello.unlink()
            process_label = 'missing-hello'
            negative = subprocess.run(command, capture_output=True, timeout=60)
            (output / 'missing-hello.stdout').write_bytes(negative.stdout)
            (output / 'missing-hello.stderr').write_bytes(negative.stderr)
            result['missing_hello_exit'] = negative.returncode
            if (negative.returncode != 1 or negative.stdout or
                    b'ARTBox: signed ART started; switch interpreter' not in negative.stderr or
                    b'signed ART runtime result: 3\n' not in negative.stderr):
                raise RuntimeError('Missing hello DEX did not fail the intended lookup')
            result['missing_hello_detected'] = True
            hello.write_bytes(make_hello()[0])
        elif libcore:
            if (native['libcore_cases'] != 17 or native['integer128_cases'] != 228 or native['threads_reaped'] != 1 or
                    native['libcore_check_ns'] <= 0 or native['integer128_check_ns'] <= 0):
                raise RuntimeError('Incomplete signed libcore execution contract')
        elif native['icu_cases'] != 8 or native['icu_check_ns'] <= 0:
            raise RuntimeError('Incomplete signed ICU execution contract')
        result['passed'] = True
    except subprocess.TimeoutExpired as error:
        (output / (process_label + '.stdout')).write_bytes(error.stdout or b'')
        (output / (process_label + '.stderr')).write_bytes(error.stderr or b'')
        if runtime:
            progress = runtime_progress(error.stderr or b'')
            if process_label == 'native': result.update(progress)
            else: result['missing_hello_progress'] = progress
        sys.stderr.buffer.write((error.stderr or b'')[-32768:])
        sys.stderr.buffer.flush()
        result['timeout'] = True
        result['timeout_process'] = process_label
        raise
    finally:
        (artifacts / ('m3-' + label + '-guest.json')).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if runtime:
        print('Signed ART JavaVM, hello DEX, managed checks and shutdown pass; missing hello is detected')
    else:
        print(('17 libcore groups and 228 integer vectors' if libcore else 'Eight native ICU groups') +
              ' pass through signed Android libraries; JavaVM startup and DEX pending')


if __name__ == '__main__': main()
