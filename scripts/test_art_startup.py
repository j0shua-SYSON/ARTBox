"""Run the original ART interpreter with verified native libraries and implementation DEX.

This native Linux reference is a prerequisite for signed Apple integration.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import subprocess
import tempfile
import time
import zipfile

from environment import ROOT, environment
from dex_fixture import make_hello
from build_art_managed_fixture import prepare_fixture


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def verify_build(directory, artifacts):
    record = read_json(directory / 'all-results.json')
    if record['profile'] != 'linux' or any(r['exit'] for r in record['results']):
        raise RuntimeError('A complete native Linux build is required: ' + str(directory))
    if digest(directory / 'corresponding-source.zip') != record['source_bundle_sha256']:
        raise RuntimeError('Corresponding source changed: ' + str(directory))
    for name, expected in record['project_sources'].items():
        if not (ROOT / name).resolve().is_relative_to(ROOT) or digest(ROOT / name) != expected:
            raise RuntimeError('Project build input changed: ' + name)
    binaries = record['link']['artifacts'] if artifacts == 'runtime' else record['linked_artifacts']
    for name, expected in binaries.items():
        if Path(name).name != name or digest(directory / name) != expected['sha256']:
            raise RuntimeError('Native binary changed: ' + name)
    return record, binaries


def prepare_classlib(directory, output):
    result = read_json(directory / 'result.json')
    bundle_path = directory / 'artbox-classlib.zip'
    if digest(bundle_path) != result['bundle_sha256']:
        raise RuntimeError('Class-library bundle changed')
    with zipfile.ZipFile(bundle_path) as bundle:
        report = json.loads(bundle.read('report.json'))
        if report != result['report']:
            raise RuntimeError('Class-library reports differ')
        for name, expected in report['project_sources'].items():
            if not (ROOT / name).resolve().is_relative_to(ROOT) or digest(ROOT / name) != expected:
                raise RuntimeError('Class-library build input changed: ' + name)
        contents = bundle.read('implementation-dex.zip')
        source = bundle.read('corresponding-source.zip')
        for name, data in [('implementation-dex.zip', contents), ('corresponding-source.zip', source)]:
            if hashlib.sha256(data).hexdigest() != report['artifacts'][name]['sha256']:
                raise RuntimeError('Class-library artifact changed: ' + name)
        (output / 'classlib-corresponding-source.zip').write_bytes(source)
        paths = []
        with zipfile.ZipFile(io.BytesIO(contents)) as dex:
            if sorted(dex.namelist()) != ['classes.dex', 'classes2.dex']:
                raise RuntimeError('Unexpected boot DEX set')
            for item in report['dex_files']:
                data = dex.read(item['name'])
                if hashlib.sha256(data).hexdigest() != item['sha256'] or len(data) != item['bytes']:
                    raise RuntimeError('Boot DEX changed: ' + item['name'])
                path = output / item['name']
                path.write_bytes(data)
                paths.append(path)
    return paths, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', type=Path)
    parser.add_argument('--native-dir', type=Path)
    parser.add_argument('--libcore-dir', type=Path)
    parser.add_argument('--classlib-dir', type=Path)
    parser.add_argument('--managed-fixture-dir', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine().lower() not in ('aarch64', 'arm64'):
        parser.error('The ART reference requires native ARM64 Linux')
    if args.timeout < 1: parser.error('--timeout must be positive')
    os.environ.update(environment())
    build = Path(os.environ['ARTBOX_BUILD_DIR'])
    runtime = (args.runtime_dir or build / 'm3/runtime-build/linux').resolve()
    native = (args.native_dir or build / 'm3/native-libraries/linux').resolve()
    core = (args.libcore_dir or build / 'm3/libcore-native/linux').resolve()
    classlib = (args.classlib_dir or build / 'm3/classlib').resolve()
    managed_fixture = (args.managed_fixture_dir or build / 'm3/managed-fixture').resolve()
    directory = (args.build_dir or build / 'm3/runtime-startup').resolve()
    directory.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='attempt-', dir=directory))
    # Test the installed filter on native code before entering any Java VM.
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/test_runtime_codegen.py'),
                    '--build-dir', str(output / 'codegen')], check=True)
    runtime_record, runtime_bins = verify_build(runtime, 'runtime')
    stack_cases = (runtime_record.get('stack_initialization_probe') or {}).get('cases', [])
    if ([case['name'] for case in stack_cases] != ['zero', 'pattern'] or
            not all(case['passed'] for case in stack_cases)):
        raise RuntimeError('Compiler stack initialization and its negative control must pass')
    native_record, _ = verify_build(native, 'native')
    core_record, core_bins = verify_build(core, 'native')
    if not native_record.get('native_check') or not core_record.get('native_check'):
        raise RuntimeError('Native dependency tests must pass before ART startup')
    if core_bins['libart.so']['sha256'] != runtime_bins['libart.so']['sha256']:
        raise RuntimeError('Native class libraries and harness use different ART builds')
    dex_files, classlib_report = prepare_classlib(classlib, output)
    managed_dex, managed_report = prepare_fixture(managed_fixture, output)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if any(r['project_commit'] != head for r in
           (runtime_record, native_record, core_record, classlib_report, managed_report)):
        raise RuntimeError('Startup inputs must come from this checkout revision')
    for name in core_bins:
        if name.endswith('.so'): shutil.copyfile(core / name, output / name)
    harness = output / 'art-linux-reference'
    shutil.copyfile(runtime / harness.name, harness)
    harness.chmod(harness.stat().st_mode | stat.S_IXUSR)
    shutil.copyfile(core / 'corresponding-source.zip', output / 'native-corresponding-source.zip')
    hello = output / 'hello.dex'
    hello_data, _ = make_hello()
    hello.write_bytes(hello_data)
    roots = {name: output / path for name, path in [
        ('ANDROID_ROOT', 'system'), ('ANDROID_ART_ROOT', 'art'), ('ANDROID_DATA', 'data'),
        ('SYSTEM_EXT_ROOT', 'system_ext'), ('ANDROID_I18N_ROOT', 'i18n'), ('ANDROID_TZDATA_ROOT', 'tzdata')]}
    for path in [*roots.values(), output / 'scratch', output / 'i18n/etc/icu']:
        path.mkdir(parents=True, exist_ok=True)
    data = native / 'i18n/etc/icu/icudt75l.dat'
    if digest(data) != native_record['native_check']['icu_data_sha256']:
        raise RuntimeError('ICU data changed')
    shutil.copyfile(data, output / 'i18n/etc/icu/icudt75l.dat')
    env = {**os.environ, **{name: str(path) for name, path in roots.items()}, 'LD_LIBRARY_PATH': str(output)}
    command = [str(harness), ':'.join(map(str, dex_files)), str(hello) + ':' + str(managed_dex), str(output)]
    record = {'project_commit': head, 'device_execution_verified': False, 'runtime_started': False,
              'managed_window_profile': runtime_record.get('managed_window', False),
              'dex_method_executed': False, 'command': command, 'roots': {k: str(v) for k, v in roots.items()},
              'hello_sha256': digest(hello), 'boot_dex': classlib_report['dex_files'],
              'harness_sha256': digest(harness), 'native_libraries': core_bins,
              'managed_fixture': managed_report,
              'source_bundle_sha256': core_record['source_bundle_sha256']}
    def save():
        (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        (directory / 'result.json').write_text(json.dumps({'attempt': output.name, 'report': record}, indent=2) + '\n', encoding='utf-8')
    save()
    # Failed probes must not create core dumps outside the captured build data.
    import resource
    def no_core_dump(): resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    started = time.monotonic()
    with (output / 'stdout.log').open('w', encoding='utf-8') as stdout, (output / 'stderr.log').open('w', encoding='utf-8') as stderr:
        try:
            process = subprocess.run(command, cwd=output, env=env, stdout=stdout, stderr=stderr,
                                     timeout=args.timeout, preexec_fn=no_core_dump)
            record['exit'] = process.returncode
        except subprocess.TimeoutExpired:
            record['timed_out'] = True
            record['exit'] = None
    record['seconds'] = time.monotonic() - started
    stdout = (output / 'stdout.log').read_text(encoding='utf-8', errors='replace')
    stderr = (output / 'stderr.log').read_text(encoding='utf-8', errors='replace')
    record['runtime_invocation_attempted'] = 'ARTBox: entering ART JNI_CreateJavaVM\n' in stdout
    record['runtime_started'] = 'ms; switch interpreter, no JIT, no profiling cache\n' in stdout
    record['dex_method_executed'] = 'ARTBox: real ART method returned the expected string\n' in stdout
    def observation(prefix):
        reports = [line.removeprefix(prefix) for line in stdout.splitlines() if line.startswith(prefix)]
        if len(reports) == 1:
            try: return json.loads(reports[0])
            except json.JSONDecodeError: pass
        return None
    record['managed_checks'] = observation('ARTBox managed checks: ')
    record['managed_window'] = observation('ARTBox managed window: ')
    window = record['managed_window']
    window_valid = (not record['managed_window_profile'] or
        (isinstance(window, dict) and window.get('memmap_contract') is True and
         type(window.get('base')) is int and window['base'] >= 0x100000000 and
         window.get('length') == 0x100000000 and type(window.get('guard')) is int and
         4096 <= window['guard'] <= 65536))
    managed = record['managed_checks']
    record['managed_checks_passed'] = (isinstance(managed, dict) and
        managed.get('heap_checksum') == 6496 and managed.get('exceptions') == 3 and
        managed.get('attachments') == 4 and type(managed.get('gc_before')) is int and
        type(managed.get('gc_after')) is int and 0 <= managed['gc_before'] < managed['gc_after'])
    record['runtime_memory'] = observation('ARTBox runtime memory: ')
    memory = record['runtime_memory']
    memory_valid = (isinstance(memory, dict) and
        all(type(memory.get(key)) is int and memory[key] > 0 for key in
            ('managed_allocated_bytes', 'process_peak_rss_kib')))
    record['lifecycle_checks_passed'] = 'ARTBox: native ART lifecycle checks passed\n' in stdout
    record['passed'] = (record['exit'] == 0 and record['runtime_started'] and record['dex_method_executed']
                        and record['managed_checks_passed'] and record['lifecycle_checks_passed'] and memory_valid
                        and window_valid)
    save()
    print(stdout, end='')
    print(stderr, end='', file=sys.stderr)
    if not record['passed']: raise RuntimeError('ART startup failed; see ' + str(output))
    print('Native Linux ART hello and managed runtime checks passed; Apple acceptance remains pending')


if __name__ == '__main__': main()
