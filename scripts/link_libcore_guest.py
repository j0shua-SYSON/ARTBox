"""Link the pinned Android class libraries into signed Apple frameworks."""
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
from libcore_guest_link import check_code, check_dependencies
from link_icu_guest import LIBRARIES as ICU_LIBRARIES

LIBRARIES = [
    ('libexpat.so', 'ARTBoxExpat', ['expat'], [], []),
    ('libandroidio.so', 'ARTBoxAndroidIO', ['androidio'], [], []),
    ('libopenjdkjvm.so', 'ARTBoxOpenJDKJVM', ['jvm'], [], []),
    ('libjavacore.so', 'ARTBoxJavaCore', ['javacore'], ['libandroidio.so', 'libexpat.so'], ['crypto']),
    ('libopenjdk.so', 'ARTBoxOpenJDK', ['openjdk'], ['libandroidio.so', 'libopenjdkjvm.so'], ['crypto', 'fdlibm'])]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', required=True, type=Path)
    parser.add_argument('--icu-dir', required=True, type=Path)
    parser.add_argument('--guest-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/libcore-guest-link'
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
            raise RuntimeError('Libcore guest command failed: ' + label)
        return result.stdout.decode('utf-8')

    def verify(path, expected):
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError('Libcore input changed: ' + str(path))
        return path

    revision = run(['git', 'rev-parse', 'HEAD'], 'revision').strip()
    if run(['git', 'status', '--porcelain'], 'worktree').strip():
        raise RuntimeError('Libcore packaging requires a clean producer revision')
    native, icu, guest, evidence = [p.resolve() for p in
                                   (args.native_dir, args.icu_dir, args.guest_dir, args.dependency_dir)]
    build = read(native / 'all-results.json')
    art = read(artifacts / 'm3-art-guest-link.json')
    icu_report = read(artifacts / 'm3-icu-guest-link.json')
    for report in (build, art, icu_report):
        if report['project_commit'] != revision:
            raise RuntimeError('Require every dependency from this producer revision')
        for name, expected in report['project_sources'].items():
            data = subprocess.check_output(['git', 'show', revision + ':' + name])
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError('Source differs from its producer revision: ' + name)
    for report in (art, icu_report):
        if report['input_revision'] != revision or report['working_tree_dirty']:
            raise RuntimeError('Require clean ART and ICU producer inputs')
    rows = build['results']
    groups = {group for _, _, selected, _, extra in LIBRARIES for group in selected + extra}
    if (build['profile'] != 'android' or len(rows) != 208 or len({r['unit'] for r in rows}) != 208 or
            any(r['exit'] for r in rows) or {r['group'] for r in rows} != groups):
        raise RuntimeError('Require all 208 selected Android libcore objects')
    bundles = {'libcore': verify(native / 'corresponding-source.zip', build['source_bundle_sha256']),
               'icu': verify(icu / 'corresponding-source.zip', icu_report['source_bundle_sha256']),
               'art': verify(guest / 'corresponding-source.zip', art['source_bundle_sha256'])}
    objects = {r['unit']: verify(native / 'objects' / (r['unit'] + '.o'), r['object_sha256']) for r in rows}
    client = build['guest_check']
    if (client['exit'] or client['unit'] != 'artbox-native-libcore-check' or client['group'] != 'guest-check' or
            '-DARTBOX_GUEST_LIBCORE' not in client['command'] or
            client['source_sha256'] != build['project_sources']['fixtures/art-runtime/native_libcore.cpp']):
        raise RuntimeError('Libcore caller differs from its reviewed source or ABI')
    objects[client['unit']] = verify(native / 'objects' / (client['unit'] + '.o'), client['object_sha256'])
    integer = build['integer128']
    if integer['pin'] != read(ROOT / 'third_party/art/libcore-builtins.json') or integer['cases'] != 228:
        raise RuntimeError('Libcore integer selection changed')
    integer_archive = verify(native / 'integer128/libartbox_uint128.a', integer['archive_sha256'])
    integer_caller = verify(native / 'integer128/check.o', integer['object_sha256'])
    verify(native / 'integer128/vectors.h', integer['header_sha256'])
    for name, expected in integer['pin']['members'].items(): verify(native / 'integer128' / name, expected)
    if integer['source_sha256'] != build['project_sources']['fixtures/libcore-integer128/check.c']:
        raise RuntimeError('Integer oracle source changed')
    bases = {'libart.so': verify(guest / 'libart.so', art['elf_sha256']),
             'libc.so': evidence / 'build/m2/bionic-startup/libc.so',
             'libm.so': evidence / 'build/m3/art-math/libm.so',
             'libdl.so': evidence / 'build/m3/guest-loader/libdl.so'}
    for name in ('libc.so', 'libm.so', 'libdl.so'): verify(bases[name], art['dependencies'][name])
    for name, path in bases.items():
        if icu_report['base_inputs'][name] != digest(path): raise RuntimeError('ICU base differs: ' + name)
    for name, _, _, _ in ICU_LIBRARIES:
        bases[name] = verify(icu / name, icu_report['libraries'][name]['elf_sha256'])
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    tc = ndk / 'toolchains/llvm/prebuilt' / host
    tool = lambda name: tc / 'bin' / (name + ('.exe' if os.name == 'nt' else ''))
    crt = tc / 'sysroot/usr/lib/aarch64-linux-android/35'
    # Same canonical non-TLS, 16 KiB layout as the ICU build.
    linker = output / 'image.ld'
    script = (ROOT / 'fixtures/bionic-dynamic/image.ld').read_text(encoding='utf-8')
    script = script.replace('KEEP(*(.init_array .init_array.*))',
                            'KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))')
    script = script.replace('*(COMMON)', '*(COMMON) . = ALIGN(16384);')
    script += '\nASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0, "Libcore TLS needs an explicit integration")\n'
    script = script.replace('    .bss (NOLOAD)', '    .tdata : { *(.tdata .tdata.*) } :writable\n'
                            '    .tbss (NOLOAD) : { *(.tbss .tbss.*) } :writable\n    .bss (NOLOAD)')
    linker.write_text(script, encoding='utf-8')
    with zipfile.ZipFile(bundles['libcore']) as source:
        spec = build['sources']['art-libcore-exports']
        expected = next(r['sha256'] for r in spec['files'] if r['path'] == 'libjavacore.map')
        data = source.read('upstream/art-libcore-exports/libjavacore.map')
    if hashlib.sha256(data).hexdigest() != expected: raise RuntimeError('Original javacore export map changed')
    exports = output / 'libjavacore.map'
    exports.write_bytes(data)
    client_exports = output / 'client.map'
    client_exports.write_text('{ global: artbox_native_libcore_check; artbox_uint128_check; local: *; };\n', encoding='utf-8')

    def response(paths, name):
        if any(any(c in str(p) for c in ('"', '\r', '\n')) for p in paths):
            raise RuntimeError('Unsafe response-file path')
        path = output / (name + '.rsp')
        path.write_text('\n'.join('"' + p.as_posix() + '"' for p in paths) + '\n', encoding='utf-8')
        return '@' + str(path)

    for group in ('crypto', 'fdlibm'):
        archive = output / ('lib' + group + '.a')
        if archive.exists(): archive.unlink()
        run([tool('llvm-ar'), 'rcs', archive, response([objects[r['unit']] for r in rows if r['group'] == group], group)],
            group + '-archive')

    def metadata(path):
        dynamic = run([tool('llvm-readelf'), '--dynamic', path], path.name + '-dynamic')
        def symbols(defined):
            text = run([tool('llvm-nm'), '-D', '--defined-only' if defined else '--undefined-only', '--format=posix', path],
                       path.name + ('-exports' if defined else '-imports'))
            return {line.split()[0]: line.split()[1] for line in text.splitlines() if line.strip()}
        return {'needed': re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]', dynamic),
                'exports': sorted(symbols(True)), 'imports': symbols(False)}

    images = {name: metadata(path) for name, path in bases.items()}
    report = {'project_commit': revision, 'input_revision': revision, 'working_tree_dirty': False,
              'scope': 'Android libcore libraries and native callers packaged; no execution',
              'objects': 208, 'runtime_executed': False, 'device_execution_verified': False,
              'base_inputs': {name: digest(path) for name, path in bases.items()},
              'libraries': {}, 'commands': commands, 'guest_check': client, 'integer128': integer,
              'linker_sha256': digest(linker), 'export_map_sha256': digest(exports),
              'crt_objects': {p.name: digest(p) for p in (crt / 'crtbegin_so.o', crt / 'crtend_so.o')}}
    notices = {name: (verify(native / 'notices' / name, value), value) for name, value in build['notices'].items()}
    primary = notices.pop('art-libcore-native.txt')

    def save():
        (artifacts / 'm3-libcore-guest-link.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    targets = [*LIBRARIES, ('libartbox_libcore_check.so', 'ARTBoxLibcoreCheck', ['guest-check'],
                           [name for name, _, _, _, _ in LIBRARIES], ['crypto', 'fdlibm'])]
    for name, framework, selected, needs, extra in targets:
        inputs = [objects[r['unit']] for r in [*rows, client] if r['group'] in selected]
        if 'guest-check' in selected: inputs.append(integer_caller)
        flags = [] if name == 'libopenjdkjvm.so' else ['-z', 'defs']
        if name == 'libjavacore.so': flags += ['--version-script=' + str(exports)]
        if 'guest-check' in selected: flags += ['--version-script=' + str(client_exports)]
        elf = output / name
        run([tool('ld.lld'), '-shared', *flags, '--hash-style=both', '--build-id=none', '-z', 'max-page-size=16384',
             '--pack-dyn-relocs=relr', '--no-relax', '-T', linker, '-soname', name,
             '--exclude-libs=libartbox_uint128.a', '--why-extract=' + str(output / (name + '-extracted.txt')),
             crt / 'crtbegin_so.o', response(inputs, name), *[output / n for n in needs],
             *[output / ('lib' + group + '.a') for group in extra], *bases.values(), integer_archive,
             crt / 'crtend_so.o', '-o', elf], name + '-link')
        images[name] = metadata(elf)
        if images[name]['needed'] != needs + list(bases): raise RuntimeError('DT_NEEDED changed: ' + name)
        if any(symbol in images[name]['exports'] for symbol in ('__udivti3', '__umodti3', '__udivmodti4')):
            raise RuntimeError('Integer helpers must remain library-local')
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
                                     name=framework, notice_name='LIBCORE-NOTICE.txt')
                item['frameworks'][target] = details
                save()
    report['dependency_scopes'] = check_dependencies(images, [name for name, _, _, _, _ in targets])
    project = ['scripts/link_libcore_guest.py', 'scripts/libcore_guest_link.py', 'scripts/libcore_builtins.py',
               'scripts/icu_guest_link.py', 'scripts/link_icu_guest.py', 'scripts/dynamic_bundle.py', 'tools/wrap_dynamic.py',
               'third_party/art/libcore-builtins.json', 'fixtures/bionic-dynamic/image.ld',
               'tests/test_libcore_guest_link.py', '.github/workflows/host-tests.yml',
               'docs/m3-libcore-native.md', 'THIRD_PARTY.md', 'LICENSE']
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in bundles.items(): archive.write(path, 'inputs/' + name + '-source.zip')
        for name, (path, _) in {'art-libcore-native.txt': primary, **notices}.items(): archive.write(path, 'notices/' + name)
        for name in project: archive.write(ROOT / name, 'artbox/' + name)
        for name, value in (('native-build', build), ('art-link', art), ('icu-link', icu_report)):
            archive.writestr('inputs/' + name + '.json', json.dumps(value, indent=2))
    report['project_sources'] = {name: digest(ROOT / name) for name in project}
    report['source_bundle_sha256'] = digest(bundle)
    save()
    print('Five Android class libraries and native caller packaged; JavaVM and DEX execution pending')


if __name__ == '__main__': main()
