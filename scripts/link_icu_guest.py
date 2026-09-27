"""Link pinned Android ICU objects against the verified ART guest and sign Apple frameworks."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from dynamic_bundle import prepare
from icu_guest_link import check_code, check_dependencies

LIBRARIES = [
    ('libnativehelper.so', 'ARTBoxNativeHelper', ['helper'], []),
    ('libicuuc.so', 'ARTBoxICUCommon', ['common', 'init', 'stubdata'], []),
    ('libicui18n.so', 'ARTBoxICUI18N', ['i18n'], ['libicuuc.so']),
    ('libicu.so', 'ARTBoxICU', ['shim'], ['libicui18n.so', 'libicuuc.so']),
    ('libicu_jni.so', 'ARTBoxICUJNI', ['jni'], ['libicui18n.so', 'libicuuc.so', 'libnativehelper.so'])]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', required=True, type=Path)
    parser.add_argument('--guest-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    parser.add_argument('--guest-report', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--input-revision', help='Locally available producer Git revision; defaults to HEAD')
    args = parser.parse_args()
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/icu-guest-link'
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    output.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(words, label):
        command = list(map(str, words))
        result = subprocess.run(command, capture_output=True)
        (output / (label + '.log')).write_bytes(result.stdout + result.stderr)
        commands.append(command)
        if result.returncode:
            sys.stderr.buffer.write(result.stdout + result.stderr)
            raise RuntimeError('ICU guest command failed: ' + label)
        return result.stdout.decode('utf-8')

    def verify(path, expected):
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError('ICU input changed: ' + str(path))
        return path

    revision = run(['git', 'rev-parse', 'HEAD'], 'revision').strip()
    dirty = bool(run(['git', 'status', '--porcelain'], 'worktree').strip())
    producer = run(['git', 'rev-parse', '--verify', (args.input_revision or 'HEAD') + '^{commit}'], 'producer').strip()

    def producer_sources(sources):
        for name, expected in sources.items():
            data = subprocess.check_output(['git', 'show', producer + ':' + name])
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError('Producer source differs from its Git revision: ' + name)

    native, guest, evidence = args.native_dir.resolve(), args.guest_dir.resolve(), args.dependency_dir.resolve()
    build = read(native / 'all-results.json')
    art = read(args.guest_report or artifacts / 'm3-art-guest-link.json')
    if (build['profile'] != 'android' or build['project_commit'] != producer or
            len(build['results']) != 480 or any(row['exit'] for row in build['results'])):
        raise RuntimeError('Require all 480 Android native dependency objects from the producer revision')
    if art['input_revision'] != producer or art['project_commit'] != producer or art['working_tree_dirty']:
        raise RuntimeError('Require the ART link from the same clean producer revision')
    producer_sources(build['project_sources'])
    producer_sources(art['project_sources'])
    bundles = {
        'icu': verify(native / 'corresponding-source.zip', build['source_bundle_sha256']),
        'art': verify(guest / 'corresponding-source.zip', art['source_bundle_sha256'])}
    rows = build['results']
    names = [row['unit'] for row in rows]
    if len(set(names)) != 480:
        raise RuntimeError('Duplicate ICU producer object')
    groups = {group for _, _, selected, _ in LIBRARIES for group in selected}
    if {row['group'] for row in rows} != groups:
        raise RuntimeError('ICU source groups differ from the reviewed selection')
    objects = {row['unit']: verify(native / 'objects' / (row['unit'] + '.o'), row['object_sha256']) for row in rows}
    bases = {
        'libart.so': verify(guest / 'libart.so', art['elf_sha256']),
        'libc.so': evidence / 'build/m2/bionic-startup/libc.so',
        'libm.so': evidence / 'build/m3/art-math/libm.so',
        'libdl.so': evidence / 'build/m3/guest-loader/libdl.so'}
    for name in ('libc.so', 'libm.so', 'libdl.so'):
        verify(bases[name], art['dependencies'][name])
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    tc = ndk / 'toolchains/llvm/prebuilt' / host
    tool = lambda name: tc / 'bin' / (name + ('.exe' if os.name == 'nt' else ''))
    crt = tc / 'sysroot/usr/lib/aarch64-linux-android/35'
    # These libraries have no compiler TLS. Do not emit an empty PT_TLS: the
    # signed wrapper and runtime correctly reject an empty template.
    script = (ROOT / 'fixtures/bionic-dynamic/image.ld').read_text(encoding='utf-8')
    script = script.replace('KEEP(*(.init_array .init_array.*))',
                            'KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))')
    script = script.replace('*(COMMON)', '*(COMMON) . = ALIGN(16384);')
    script += '\nASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0, "ICU TLS needs an explicit integration")\n'
    # Define the sections so the assertion is valid even when no input uses TLS.
    script = script.replace('    .bss (NOLOAD)', '    .tdata : { *(.tdata .tdata.*) } :writable\n'
                            '    .tbss (NOLOAD) : { *(.tbss .tbss.*) } :writable\n    .bss (NOLOAD)')
    linker = output / 'image.ld'
    linker.write_text(script, encoding='utf-8')

    def symbols(path, defined):
        text = run([tool('llvm-nm'), '-D', '--defined-only' if defined else '--undefined-only',
                    '--format=posix', path], path.name + ('-exports' if defined else '-imports'))
        return {line.split()[0]: line.split()[1] for line in text.splitlines() if line.strip()}

    def metadata(path):
        dynamic = run([tool('llvm-readelf'), '--dynamic', path], path.name + '-dynamic')
        return {'needed': re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]', dynamic),
                'exports': sorted(symbols(path, True)), 'imports': symbols(path, False)}

    images = {name: metadata(path) for name, path in bases.items()}
    report = {'project_commit': revision, 'input_revision': producer, 'working_tree_dirty': dirty,
              'scope': 'Five Android ICU/JNI libraries linked and packaged; no execution',
              'objects': 480, 'runtime_executed': False, 'device_execution_verified': False,
              'base_inputs': {name: digest(path) for name, path in bases.items()},
              'libraries': {}, 'commands': commands,
              'crt_objects': {p.name: digest(p) for p in (crt / 'crtbegin_so.o', crt / 'crtend_so.o')}}
    notices = {name: (verify(native / 'notices' / name, value), value) for name, value in build['notices'].items()}
    primary = notices.pop('art-icu-native.txt')

    def save():
        (artifacts / 'm3-icu-guest-link.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    for name, framework, selected, needs in LIBRARIES:
        inputs = [objects[row['unit']] for row in rows if row['group'] in selected]
        if any('"' in str(p) or '\n' in str(p) for p in inputs):
            raise RuntimeError('Unsafe object response-file path')
        response = output / (name + '.rsp')
        response.write_text('\n'.join('"' + p.as_posix() + '"' for p in inputs) + '\n', encoding='utf-8')
        elf = output / name
        run([tool('ld.lld'), '-shared', '-z', 'defs', '--hash-style=both', '--build-id=none',
             '-z', 'max-page-size=16384', '--pack-dyn-relocs=relr', '--no-relax', '-T', linker,
             '-soname', name, crt / 'crtbegin_so.o', '@' + str(response),
             *[output / n for n in needs], *bases.values(), crt / 'crtend_so.o', '-o', elf], name + '-link')
        images[name] = metadata(elf)
        if images[name]['needed'] != needs + list(bases):
            raise RuntimeError('Linked DT_NEEDED list changed: ' + name)
        boundary = check_code(run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf], name + '-instructions'))
        packed = output / (name + '-pack')
        run([sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', elf, packed], name + '-pack')
        item = {'objects': len(inputs), 'elf_sha256': digest(elf), 'elf_bytes': elf.stat().st_size,
                'dynamic': images[name], 'boundary': boundary, 'layout': read(packed / 'layout.json'),
                'framework_name': framework, 'frameworks': {}}
        report['libraries'][name] = item
        save()
        if sys.platform == 'darwin':
            for target in ('macos', 'ios'):
                _, details = prepare(packed, output / target, target, *primary, notices,
                                     name=framework, notice_name='ICU-NOTICE.txt')
                item['frameworks'][target] = details
                save()
    report['dependency_scopes'] = check_dependencies(images, [name for name, _, _, _ in LIBRARIES])
    data_name = 'icu4c/source/stubdata/icudt75l.dat'
    expected = next(row['sha256'] for row in build['sources']['art-icu-native']['files'] if row['path'] == data_name)
    with zipfile.ZipFile(bundles['icu']) as source:
        data = source.read('upstream/art-icu-native/' + data_name)
    if hashlib.sha256(data).hexdigest() != expected:
        raise RuntimeError('ICU data differs from its pinned source')
    destination = output / 'i18n/etc/icu/icudt75l.dat'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    report['data'] = {'path': destination.relative_to(output).as_posix(), 'sha256': expected, 'bytes': len(data)}
    project = ['scripts/link_icu_guest.py', 'scripts/icu_guest_link.py', 'scripts/dynamic_bundle.py',
               'tools/wrap_dynamic.py', 'fixtures/bionic-dynamic/image.ld', 'tests/test_icu_guest_link.py',
               '.github/workflows/host-tests.yml', 'docs/m3-native-libraries.md', 'THIRD_PARTY.md', 'LICENSE']
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in bundles.items(): archive.write(path, 'inputs/' + name + '-source.zip')
        for name, (path, _) in {'art-icu-native.txt': primary, **notices}.items(): archive.write(path, 'notices/' + name)
        for name in project: archive.write(ROOT / name, 'artbox/' + name)
        archive.writestr('inputs/native-build.json', json.dumps(build, indent=2))
        archive.writestr('inputs/art-link.json', json.dumps(art, indent=2))
    report['project_sources'] = {name: digest(ROOT / name) for name in project}
    report['source_bundle_sha256'] = digest(bundle)
    save()
    print('Five Android ICU/JNI libraries linked from 480 verified objects; ' +
          ('Mac/iOS 15 frameworks signed; execution pending' if sys.platform == 'darwin' else 'Apple packaging and execution pending'))


if __name__ == '__main__': main()
