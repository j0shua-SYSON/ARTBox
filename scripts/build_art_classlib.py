"""Build pinned AOSP implementation DEX and corresponding source, without ART execution."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time
import zipfile
import zlib
from environment import ROOT, environment
from sources import obtain


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def run(command, log):
    result = subprocess.run([str(a) for a in command], cwd=ROOT, capture_output=True, encoding='utf-8')
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(f'{log.name} failed ({result.returncode}); see {log}')
    return result.stdout


def annotation_keys(aconfig, config):
    """Generate annotation names only; runtime feature accessors are intentionally absent."""
    if hashlib.sha256(aconfig).hexdigest() != config['icu_aconfig_sha256']:
        raise RuntimeError('ICU flag declarations differ from the reviewed input')
    text = aconfig.decode('utf-8')
    packages = re.findall(r'^package:\s*"([a-z0-9_.]+)"\s*$', text, re.M)
    names = re.findall(r'^\s+name:\s*"([a-z0-9_]+)"\s*$', text, re.M)
    if len(packages) != 1 or {name: packages[0] + '.' + name for name in names} != config['icu_annotation_keys']:
        raise RuntimeError('ICU annotation keys differ from the reviewed declarations')
    return ('// ARTBox generated annotation keys from the pinned ICU aconfig declarations.\n'
            '// No runtime flag accessor is supplied.\npackage ' + packages[0] + ';\n'
            'public final class Flags {\n' + ''.join(
                '    public static final String FLAG_' + name.upper() + ' = "' + key + '";\n'
                for name, key in config['icu_annotation_keys'].items()) + '    private Flags() {}\n}\n')


def dex_envelope(name, data):
    """Check the DEX039 envelope; this does not replace the AOSP verifier."""
    if len(data) < 112 or data[:8] != b'dex\n039\0' or \
            struct.unpack_from('<I', data, 32)[0] != len(data) or \
            struct.unpack_from('<I', data, 36)[0] != 112 or \
            struct.unpack_from('<I', data, 40)[0] != 0x12345678 or \
            hashlib.sha1(data[32:]).digest() != data[12:32] or \
            zlib.adler32(data[12:]) != struct.unpack_from('<I', data, 8)[0]:
        raise RuntimeError('Invalid DEX envelope: ' + name)
    return {'name': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'classes': struct.unpack_from('<I', data, 96)[0],
            'methods': struct.unpack_from('<I', data, 88)[0], 'version': '039'}


def write_zip(path, files):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, source in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2024, 9, 3, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, source.read_bytes())


def build(args):
    config_path = ROOT / 'third_party/art/classlib.json'
    config = json.loads(config_path.read_text(encoding='utf-8'))
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    directory = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/classlib'
    directory.mkdir(parents=True, exist_ok=True)
    # Preserve previous results and failed compiler diagnostics; never mix stale classes.
    attempt = Path(tempfile.mkdtemp(prefix='attempt-', dir=directory))
    java_home = args.java_home
    if java_home is None:
        java_home = next((Path(os.environ[key]) for key in
                          ('ARTBOX_JAVA_HOME', 'JAVA_HOME_17_ARM64', 'JAVA_HOME_17_X64', 'JAVA_HOME')
                          if os.environ.get(key)), None)
    if java_home is None:
        raise RuntimeError('Set --java-home or ARTBOX_JAVA_HOME to an existing JDK 17')
    java = java_home / 'bin' / ('java.exe' if os.name == 'nt' else 'java')
    if not java.is_file():
        raise RuntimeError('JDK java executable is missing: ' + str(java))
    cxx = args.cxx or os.environ.get('CXX') or shutil.which('c++') or shutil.which('clang++')
    if not cxx:
        raise RuntimeError('Set --cxx to an existing GCC or Clang C++ compiler')
    home = Path(os.environ['ARTBOX_CACHE_DIR']) / 'java-home'
    home.mkdir(exist_ok=True)
    vm = ['-Xmx' + str(args.java_heap_mb) + 'm', '-XX:-UsePerfData', '-Dfile.encoding=UTF-8', '-Duser.home=' + str(home),
          '-Djava.io.tmpdir=' + os.environ['ARTBOX_TEMP_DIR'],
          '-XX:ErrorFile=' + str(attempt / 'java-error-%p.log')]
    compiler_version = run([java, *vm, '-m', 'jdk.compiler/com.sun.tools.javac.Main', '-version'], attempt / 'javac-version.log').strip()
    if not re.fullmatch(r'javac 17(?:\.[0-9]+)*(?:[+-][^\s]+)?', compiler_version):
        raise RuntimeError('This source selection requires JDK 17: ' + compiler_version)
    native_version = run([cxx, '--version'], attempt / 'cxx-version.log').splitlines()[0]
    sources = {name: obtain(name) for name in config['java_sources'] + config['build_sources']}
    generated = attempt / 'generated'
    constants = generated / 'com/android/org/conscrypt/NativeConstants.java'
    flags = generated / 'com/android/icu/Flags.java'
    constants.parent.mkdir(parents=True, exist_ok=True)
    flags.parent.mkdir(parents=True, exist_ok=True)
    native_generator = attempt / ('conscrypt-constants.exe' if os.name == 'nt' else 'conscrypt-constants')
    run([cxx, '-std=c++17', '-O2', '-I', sources['boringssl-classlib'] / 'src/include',
         sources['conscrypt-classlib'] / config['conscrypt_generator'], '-o', native_generator], attempt / 'constants-compile.log')
    generated_text = run([native_generator, 'com.android.org.conscrypt'], attempt / 'constants-output.log')
    values = dict(re.findall(r'static final int ([A-Z0-9_]+) = (-?\d+);', generated_text))
    if values != config['conscrypt_constants']:
        raise RuntimeError('Conscrypt constants differ from the reviewed Android values')
    constants.write_bytes(generated_text.encode('utf-8'))
    flags.write_bytes(annotation_keys((sources['icu-classlib'] / 'icu.aconfig').read_bytes(), config).encode('utf-8'))
    java_inputs = sorted(sources[name] / entry['path'] for name in config['java_sources']
                         for entry in specs[name]['files'] if entry['path'].endswith('.java'))
    java_inputs += [constants, flags]
    if len(java_inputs) != config['expected_java_inputs']:
        raise RuntimeError('Class-library source selection count changed')
    classes, empty = attempt / 'classes', attempt / 'empty'
    classes.mkdir()
    empty.mkdir()
    patch_paths = [sources[name] for name in config['java_sources']] + [generated]
    javac_args = ['-encoding', 'UTF-8', '-source', '17', '-target', '17', '-g', '-XDstringConcat=inline',
                  '--system', 'none', '--patch-module', 'java.base=' + os.pathsep.join(p.as_posix() for p in patch_paths),
                  '-classpath', empty.as_posix(), '-sourcepath', empty.as_posix(), '-proc:none',
                  '-d', classes.as_posix(), *[p.as_posix() for p in java_inputs]]
    argfile = attempt / 'javac.args'
    argfile.write_text('\n'.join('"' + arg.replace('\\', '\\\\').replace('"', '\\"') + '"' for arg in javac_args) + '\n', encoding='utf-8')
    start = time.monotonic()
    run([java, *vm, '-m', 'jdk.compiler/com.sun.tools.javac.Main', '@' + str(argfile)], attempt / 'javac.log')
    javac_seconds = time.monotonic() - start
    for name in config['required_classes']:
        if not (classes / (name + '.class')).is_file():
            raise RuntimeError('Required implementation class is absent: ' + name)
    class_files = {p.relative_to(classes).as_posix(): p for p in classes.rglob('*.class') if p.name != 'module-info.class'}
    if len(class_files) != config['expected_class_files']:
        raise RuntimeError('Compiled implementation class count changed')
    input_jar = attempt / 'implementation-classes.jar'
    write_zip(input_jar, class_files)
    r8 = sources['r8-classlib'] / 'r8.jar'
    version = run([java, *vm, '-cp', r8, 'com.android.tools.r8.D8', '--version'], attempt / 'd8-version.log').strip()
    d8_vm = [arg for arg in vm if not arg.startswith('-Xmx')] + ['-Xmx' + str(args.dex_heap_mb) + 'm',
             '-XX:TieredStopAtLevel=1', '-Dcom.android.tools.r8.emitRecordAnnotationsInDex',
             '-Dcom.android.tools.r8.emitPermittedSubclassesAnnotationsInDex']
    dex_zip = attempt / 'implementation-dex.zip'
    start = time.monotonic()
    run([java, *d8_vm, '-cp', r8, 'com.android.tools.r8.D8', '--android-platform-build',
         '--min-api', str(config['android_min_api']), '--thread-count', str(args.jobs), '--release',
         '--output', dex_zip, input_jar], attempt / 'd8.log')
    d8_seconds = time.monotonic() - start
    if 'Warning:' in (attempt / 'd8.log').read_text(encoding='utf-8'):
        raise RuntimeError('D8 emitted a diagnostic requiring review; see ' + str(attempt / 'd8.log'))
    with zipfile.ZipFile(dex_zip) as archive:
        if sorted(archive.namelist()) != ['classes.dex', 'classes2.dex']:
            raise RuntimeError('Unexpected D8 output entries')
        dex_files = [dex_envelope(name, archive.read(name)) for name in sorted(archive.namelist()) if name.endswith('.dex')]
    if sum(item['classes'] for item in dex_files) != config['expected_dex_classes']:
        raise RuntimeError('D8 implementation class count changed')
    source_files = {f'upstream/{name}/{entry["path"]}': sources[name] / entry['path']
                    for name in sources for entry in specs[name]['files'] if entry['path'] != 'r8.jar'}
    project_inputs = ('LICENSE', 'THIRD_PARTY.md', 'docs/m3-classlib.md', 'scripts/build_art_classlib.py',
                      'scripts/environment.py', 'scripts/sources.py', 'third_party/sources.json',
                      'third_party/art/classlib.json')
    for name in project_inputs:
        source_files['artbox/' + name] = ROOT / name
    source_files.update({'generated/' + p.relative_to(generated).as_posix(): p for p in (constants, flags)})
    source_kit = attempt / 'corresponding-source.zip'
    write_zip(source_kit, source_files)
    notices = {'ARTBOX-LICENSE.txt': ROOT / 'LICENSE', 'LIBCORE-LICENSE.txt': sources['libcore-classlib'] / 'LICENSE',
               'LIBCORE-NOTICE.txt': sources['libcore-classlib'] / 'NOTICE',
               'OPENJDK-NOTICE.txt': sources['libcore-classlib'] / 'ojluni/src/main/NOTICE',
               'ICU-LICENSE.txt': sources['icu-classlib'] / 'LICENSE',
               'APACHE-2.0.txt': sources['conscrypt-classlib'] / 'LICENSE',
               'CONSCRYPT-NOTICE.txt': sources['conscrypt-classlib'] / 'NOTICE',
               'OKHTTP-LICENSE.txt': sources['okhttp-classlib'] / 'LICENSE.txt',
               'OKIO-LICENSE.txt': sources['okhttp-classlib'] / 'okio/LICENSE.txt',
               'BORINGSSL-NOTICE.txt': sources['boringssl-classlib'] / 'NOTICE',
               'R8-LICENSE.txt': sources['r8-classlib'] / 'LICENSE', 'R8-NOTICE.txt': sources['r8-classlib'] / 'NOTICE'}
    for name, path in notices.items():
        (attempt / name).write_bytes(path.read_bytes())
    record = {'scope': config['scope'], 'runtime_executed': False, 'aosp_verifier_executed': False,
              'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'jdk': compiler_version, 'native_compiler': native_version, 'd8': version,
              'java_inputs': len(java_inputs), 'class_files': len(class_files), 'dex_files': dex_files,
              'min_android_api': config['android_min_api'], 'javac_seconds': javac_seconds, 'd8_seconds': d8_seconds,
              'configuration_sha256': digest(config_path), 'sources': {name: specs[name] for name in sources},
              'project_sources': {name: digest(ROOT / name) for name in project_inputs},
              'generated_sha256': {p.relative_to(generated).as_posix(): digest(p) for p in (constants, flags)},
              'notices': {name: digest(path) for name, path in notices.items()},
              'artifacts': {p.name: {'sha256': digest(p), 'bytes': p.stat().st_size} for p in (input_jar, dex_zip, source_kit)}}
    save(attempt / 'report.json', record)
    bundle = directory / 'artbox-classlib.zip'
    write_zip(bundle, {p.name: p for p in [dex_zip, source_kit, attempt / 'report.json',
                                          *[attempt / name for name in notices]]})
    save(directory / 'result.json', {'attempt': attempt.name, 'bundle_sha256': digest(bundle), 'report': record})
    print(f'AOSP class-library build: {len(java_inputs)} inputs, {len(class_files)} classes, '
          f'{sum(item["classes"] for item in dex_files)} DEX classes; ART execution pending')
    print(bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--cxx', help='GCC or Clang C++ compiler executable')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--java-heap-mb', type=int, default=768)
    parser.add_argument('--dex-heap-mb', type=int, default=1024)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8 or not 512 <= args.java_heap_mb <= 8192 or not 512 <= args.dex_heap_mb <= 8192:
        parser.error('Use 1..8 jobs and 512..8192 MB compiler heaps')
    os.environ.update(environment())
    build(args)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print('ARTBox class library: ' + str(error), file=sys.stderr)
        sys.exit(1)
