"""Build original managed acceptance inputs; host JDK checks are not ART execution."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

from environment import ROOT, environment
from sources import obtain
from build_art_classlib import dex_envelope, digest, run, save, write_zip


PROJECT_INPUTS = (
    'LICENSE', 'THIRD_PARTY.md', 'docs/m3-managed-checks.md',
    'fixtures/art-runtime/RuntimeChecks.java', 'fixtures/art-runtime/RuntimeChecksHost.java',
    'fixtures/art-runtime/managed_checks.cpp', 'fixtures/art-runtime/linux_reference.cpp',
    'scripts/build_art_managed_fixture.py', 'scripts/build_art_classlib.py',
    'scripts/environment.py', 'scripts/sources.py', 'scripts/jdk.py',
    'third_party/jdk.json', 'third_party/sources.json',
)


def prepare_fixture(directory, output):
    """Validate the producer and current sources before exposing any DEX to ART."""
    result = json.loads((directory / 'result.json').read_text(encoding='utf-8'))
    bundle = directory / 'artbox-runtime-checks.zip'
    if digest(bundle) != result['bundle_sha256']:
        raise RuntimeError('Managed fixture bundle changed')
    with zipfile.ZipFile(bundle) as archive:
        report = json.loads(archive.read('report.json'))
        if report != result['report'] or set(report['project_sources']) != set(PROJECT_INPUTS):
            raise RuntimeError('Managed fixture provenance is incomplete or differs')
        for name, expected in report['project_sources'].items():
            if digest(ROOT / name) != expected:
                raise RuntimeError('Managed fixture project input changed: ' + name)
        data = archive.read('runtime-checks.dex')
        source = archive.read('corresponding-source.zip')
        for name, payload in [('runtime-checks.dex', data), ('corresponding-source.zip', source)]:
            expected = report['artifacts'][name]
            if len(payload) != expected['bytes'] or hashlib.sha256(payload).hexdigest() != expected['sha256']:
                raise RuntimeError('Managed fixture payload changed: ' + name)
        if dex_envelope('runtime-checks.dex', data) != report['dex']:
            raise RuntimeError('Managed fixture DEX envelope differs')
        if not report['host_logic_checked'] or report['art_execution_verified']:
            raise RuntimeError('Managed fixture producer has an invalid execution claim')
    dex = output / 'runtime-checks.dex'
    dex.write_bytes(data)
    (output / 'managed-corresponding-source.zip').write_bytes(source)
    return dex, report


def build(args):
    directory = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/managed-fixture').resolve()
    directory.mkdir(parents=True, exist_ok=True)
    attempt = Path(tempfile.mkdtemp(prefix='attempt-', dir=directory))
    java_home, distribution = args.java_home, None
    if args.fetch_jdk:
        from jdk import obtain as obtain_jdk
        java_home, distribution = obtain_jdk()
    if java_home is None:
        java_home = next((Path(os.environ[name]) for name in ('ARTBOX_JAVA_HOME', 'JAVA_HOME')
                          if os.environ.get(name)), None)
    if java_home is None:
        raise RuntimeError('Supply --java-home, ARTBOX_JAVA_HOME or --fetch-jdk')
    java = java_home.resolve() / 'bin' / ('java.exe' if os.name == 'nt' else 'java')
    if not java.is_file(): raise RuntimeError('JDK java executable is missing')
    home = Path(os.environ['ARTBOX_CACHE_DIR']) / 'java-home'
    home.mkdir(exist_ok=True)
    vm = ['-Xmx512m', '-XX:-UsePerfData', '-Dfile.encoding=UTF-8', '-Duser.home=' + str(home),
          '-Djava.io.tmpdir=' + os.environ['ARTBOX_TEMP_DIR'],
          '-XX:ErrorFile=' + str(attempt / 'java-error-%p.log')]
    version = run([java, *vm, '-m', 'jdk.compiler/com.sun.tools.javac.Main', '-version'],
                  attempt / 'javac-version.log').strip()
    if not re.fullmatch(r'javac 17(?:\.[0-9]+)*(?:[+-][^\s]+)?', version):
        raise RuntimeError('Managed fixture requires JDK 17')
    payload, host = attempt / 'payload-classes', attempt / 'host-classes'
    payload.mkdir(); host.mkdir()
    compiler = [java, *vm, '-m', 'jdk.compiler/com.sun.tools.javac.Main',
                '-encoding', 'UTF-8', '-source', '17', '-target', '17', '-proc:none',
                '-XDstringConcat=inline']
    commands = [
        [*compiler, '-d', payload, ROOT / 'fixtures/art-runtime/RuntimeChecks.java'],
        [*compiler, '-cp', payload, '-d', host, ROOT / 'fixtures/art-runtime/RuntimeChecksHost.java'],
        [java, *vm, '-Xint', '-cp', os.pathsep.join(map(str, (payload, host))), 'artbox.RuntimeChecksHost'],
    ]
    outputs = [run(command, attempt / (name + '.log')) for name, command in
               zip(('payload-javac', 'host-javac', 'host-logic'), commands)]
    if outputs[-1].strip() != 'Host JDK fixture logic passed; ART execution unverified':
        raise RuntimeError('Host fixture logic did not pass')
    jar = attempt / 'runtime-checks.jar'
    write_zip(jar, {p.relative_to(payload).as_posix(): p for p in sorted(payload.rglob('*.class'))})
    tools = obtain('r8-classlib')
    r8 = tools / 'r8.jar'
    d8_version = run([java, *vm, '-cp', r8, 'com.android.tools.r8.D8', '--version'],
                     attempt / 'd8-version.log').strip()
    dex_zip = attempt / 'runtime-checks-dex.zip'
    command = [java, *vm, '-cp', r8, 'com.android.tools.r8.D8', '--min-api', '31',
               '--thread-count', '1', '--release', '--output', dex_zip, jar]
    commands.append(command)
    run(command, attempt / 'd8.log')
    if 'Warning:' in (attempt / 'd8.log').read_text(encoding='utf-8'):
        raise RuntimeError('D8 diagnostic requires review')
    with zipfile.ZipFile(dex_zip) as archive:
        if archive.namelist() != ['classes.dex']: raise RuntimeError('Unexpected managed DEX output')
        data = archive.read('classes.dex')
    dex = attempt / 'runtime-checks.dex'
    dex.write_bytes(data)
    envelope = dex_envelope(dex.name, data)
    source_files = {'artbox/' + name: ROOT / name for name in PROJECT_INPUTS}
    source_files.update({'notices/R8-LICENSE.txt': tools / 'LICENSE', 'notices/R8-NOTICE.txt': tools / 'NOTICE'})
    source = attempt / 'corresponding-source.zip'
    write_zip(source, source_files)
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    report = {'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'host_logic_checked': True, 'art_execution_verified': False,
              'jdk': version, 'jdk_distribution': distribution, 'd8': d8_version,
              'r8': specs['r8-classlib'], 'r8_jar_sha256': digest(r8),
              'commands': [list(map(str, command)) for command in commands], 'dex': envelope,
              'project_sources': {name: digest(ROOT / name) for name in PROJECT_INPUTS},
              'artifacts': {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in (dex, jar, source)}}
    save(attempt / 'report.json', report)
    bundle = directory / 'artbox-runtime-checks.zip'
    write_zip(bundle, {p.name: p for p in (dex, jar, source, attempt / 'report.json')})
    save(directory / 'result.json', {'attempt': attempt.name, 'bundle_sha256': digest(bundle), 'report': report})
    print(f'Managed fixture: {envelope["classes"]} classes, {len(data)} DEX bytes; ART execution pending')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    jdk = parser.add_mutually_exclusive_group()
    jdk.add_argument('--java-home', type=Path)
    jdk.add_argument('--fetch-jdk', action='store_true', help='Fetch the pinned portable JDK into the build cache')
    args = parser.parse_args()
    os.environ.update(environment())
    build(args)


if __name__ == '__main__': main()
