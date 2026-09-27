"""Link and sign the full ART guest from verified build artifacts; no execution."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile
from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from dynamic_bundle import prepare
from art_guest_link import check_code, check_imports


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-dir', required=True, type=Path)
    parser.add_argument('--dependency-dir', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--input-revision', help='Locally available Git commit that produced the CI artifacts; defaults to HEAD')
    args = parser.parse_args()
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/art-guest-link'
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    output.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(words, label):
        command = list(map(str, words))
        result = subprocess.run(command, capture_output=True)
        (output / (label + '.log')).write_bytes(result.stdout + result.stderr)
        commands.append(command)
        if result.returncode: raise RuntimeError('ART guest link failed: ' + label)
        return result.stdout.decode('utf-8')

    def verify(path, expected):
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError('ART link input changed: ' + str(path))
        return path

    revision = run(['git', 'rev-parse', 'HEAD'], 'revision').strip()
    dirty = bool(run(['git', 'status', '--porcelain'], 'worktree').strip())
    input_revision = run(['git', 'rev-parse', '--verify', (args.input_revision or 'HEAD') + '^{commit}'], 'input-revision').strip()
    def producer_sources(sources):
        for name, expected in sources.items():
            data = subprocess.check_output(['git', 'show', input_revision + ':' + name])
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError('Producer source differs from its Git revision: ' + name)
    runtime, evidence = args.runtime_dir.resolve(), args.dependency_dir.resolve()
    build = read(runtime / 'all-results.json')
    if (build['project_commit'] != input_revision or build['profile'] != 'android' or
            not build.get('native_guest') or not build['managed_window'] or len(build['results']) != 462 or
            build['excluded_host_owned_units'] != ['artbox-vm', 'artbox-native-vm'] or
            any(r['exit'] for r in build['results'])):
        raise RuntimeError('Require the complete native guest build from this revision')
    producer_sources(build['project_sources'])
    source_archives = {'runtime': verify(runtime / 'corresponding-source.zip', build['source_bundle_sha256'])}
    units = [r['unit'] for r in build['results']]
    if len(set(units)) != 462 or any(n in units for n in build['excluded_host_owned_units']):
        raise RuntimeError('Guest object ownership or uniqueness changed')
    inputs = [verify(runtime / 'objects' / (r['unit'] + '.o'), r['object_sha256']) for r in build['results']]
    expected_tls = read(ROOT / 'third_party/art/native-tls.json')
    tls_rows = {r['unit']: r for r in build['results'] if 'compiler_tls' in r}
    if set(tls_rows) != set(expected_tls): raise RuntimeError('Missing guest TLS producer evidence')
    for name, rule in expected_tls.items():
        row = tls_rows[name]
        if row['source_sha256'] != rule['source_sha256'] or row['compiler_tls']['edits'] != {
                k: rule[k] for k in ('tlsdesc_calls', 'tp_reads_replaced')}:
            raise RuntimeError('Guest TLS selection changed: ' + name)
        if row['compiler_tls']['adapted']['object_sha256'] != row['object_sha256']:
            raise RuntimeError('The linked TLS object is not its verified adaptation')
    reports = {}
    for name, path in [('bionic', 'm2-bionic-startup.json'), ('math', 'm3-art-math.json'),
                       ('libdl', 'm3-guest-loader.json'), ('context', 'm3-unwind-context.json')]:
        record = read(evidence / 'artifacts' / path)
        if record['project_commit'] != input_revision: raise RuntimeError('Dependency revision differs: ' + name)
        producer_sources(record.get('project_sources', {}))
        reports[name] = record
    bionic, math, libdl, context = [reports[n] for n in ('bionic', 'math', 'libdl', 'context')]
    if any(bionic[mode]['art_libc_cases'] != 73 or bionic[mode]['vfork_cases'] != 30 or
           bionic[mode]['libcore_frontend_cases'] != 75 or bionic[mode]['unlink_cases'] != 29 or bionic['acceptance'][mode]['passed'] != 328 or
           not bionic['acceptance'][mode]['success'] for mode in ('native', 'sampled_native')):
        raise RuntimeError('Bionic runtime acceptance is incomplete')
    if math['native'] != {'cases': 130, 'first_failure': 0, 'cleanup': True} or libdl['native'] != {
            'cases': 32, 'thread_error_checks': 6, 'cleanup': True}:
        raise RuntimeError('Native math or loader acceptance is incomplete')
    if context['native'] != {'checks': 4, 'failure_mask': 0, 'expected_rejection': False, 'cleanup': True}:
        raise RuntimeError('Native LLVM context acceptance is incomplete')
    deps = {
        'libc.so': verify(evidence / 'build/m2/bionic-startup/libc.so', bionic['images']['libc']['elf_sha256']),
        'libm.so': verify(evidence / 'build/m3/art-math/libm.so', math['binaries']['library']['sha256']),
        'libdl.so': verify(evidence / 'build/m3/guest-loader/libdl.so', libdl['binaries']['libdl']['sha256'])}
    for name, folder in [('math', 'art-math'), ('libdl', 'guest-loader'), ('context', 'unwind-context')]:
        source_archives[name] = verify(evidence / 'build/m3' / folder / 'corresponding-source.zip',
                                       reports[name]['source_bundle_sha256'])
    restore = verify(evidence / 'build/m3/unwind-context/UnwindRegistersRestore.adapted.o',
                     context['adapted_object_sha256'])
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    tc = ndk / 'toolchains/llvm/prebuilt' / host
    tool = lambda name: tc / 'bin' / (name + ('.exe' if os.name == 'nt' else ''))
    archives = [tc / 'sysroot/usr/lib/aarch64-linux-android' / n for n in ('libc++_static.a', 'libc++abi.a')]
    archives += [tc / 'lib/clang/19/lib/linux/aarch64/libunwind.a',
                 tc / 'lib/clang/19/lib/linux/libclang_rt.builtins-aarch64-android.a']
    crt = tc / 'sysroot/usr/lib/aarch64-linux-android/35'
    # Match the already verified Bionic/TLSDESC layout; only data is relocated.
    script = (ROOT / 'fixtures/bionic-dynamic/image.ld').read_text(encoding='utf-8')
    script = script.replace('KEEP(*(.init_array .init_array.*))',
                            'KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))')
    script = script.replace('*(COMMON)', '*(COMMON) . = ALIGN(16384);')
    script = script.replace('    stack PT_GNU_STACK', '    tls PT_TLS FLAGS(4);\n    stack PT_GNU_STACK')
    script = script.replace('    .bss (NOLOAD)', '    .tdata : { *(.tdata .tdata.*) } :writable :tls\n'
                            '    .tbss (NOLOAD) : { *(.tbss .tbss.*) } :writable :tls\n    .bss (NOLOAD)')
    linker_script = output / 'image.ld'
    linker_script.write_text(script, encoding='utf-8')
    response = output / 'objects.rsp'
    if any('"' in str(p) or '\n' in str(p) for p in inputs): raise RuntimeError('Unsafe response-file path')
    response.write_text('\n'.join('"' + p.as_posix() + '"' for p in inputs) + '\n', encoding='utf-8')
    elf = output / 'libart.so'
    run([tool('ld.lld'), '-shared', '--hash-style=both', '--build-id=none', '-z', 'max-page-size=16384',
         '--pack-dyn-relocs=relr', '--no-relax', '-T', linker_script, '-soname', 'libart.so',
         '--why-extract=' + str(output / 'extracted.txt'), crt / 'crtbegin_so.o', '@' + str(response), restore,
         '--start-group', *archives, '--end-group', *deps.values(), crt / 'crtend_so.o', '-o', elf], 'link')
    if 'libunwind.a(UnwindRegistersRestore.S.o)' in (output / 'extracted.txt').read_text():
        raise RuntimeError('The original context restoration would overwrite Apple x18')
    imports = {row.split()[0]: row.split()[1] for row in run([
        tool('llvm-nm'), '-D', '--undefined-only', '--format=posix', elf], 'imports').splitlines()}
    exports = set()
    for name, dependency in deps.items():
        exports.update(row.split()[0] for row in run([tool('llvm-nm'), '-D', '--defined-only',
                        '--format=posix', dependency], name + '-exports').splitlines())
    remaining = check_imports(imports, exports)
    boundary = check_code(run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf], 'instructions'))
    run([sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', elf, output / 'pack'], 'pack')
    notices = {name: (verify(runtime / 'notices' / name, expected), expected) for name, expected in build['notices'].items()}
    art_notice = notices.pop('art-runtime.txt')
    project = ['scripts/link_art_guest.py', 'scripts/art_guest_link.py', 'fixtures/bionic-dynamic/image.ld',
               '.github/workflows/host-tests.yml', 'THIRD_PARTY.md', 'LICENSE']
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name, path in source_archives.items(): z.write(path, 'inputs/' + name + '-source.zip')
        for name, (path, _) in {'art-runtime.txt': art_notice, **notices}.items(): z.write(path, 'notices/' + name)
        for name in project: z.write(ROOT / name, 'artbox/' + name)
        z.writestr('inputs/runtime-build.json', json.dumps(build, indent=2))
        for name, record in reports.items(): z.writestr('inputs/' + name + '-report.json', json.dumps(record, indent=2))
    report = {'project_commit': revision, 'input_revision': input_revision,
              'working_tree_dirty': dirty,
              'scope': 'Full ART guest link and signed packaging; no ART execution',
              'objects': len(inputs), 'compiler_tls_units': sorted(tls_rows), 'host_imports': remaining,
              'dynamic_imports': imports, 'boundary': boundary, 'commands': commands,
              'elf_sha256': digest(elf), 'elf_bytes': elf.stat().st_size,
              'dependencies': {n: digest(p) for n, p in deps.items()},
              'archives': {p.relative_to(tc).as_posix(): digest(p) for p in archives},
              'crt_objects': {p.name: digest(p) for p in (crt / 'crtbegin_so.o', crt / 'crtend_so.o')},
              'adapted_restore_sha256': digest(restore), 'original_restore_excluded': True,
              'project_sources': {p: digest(ROOT / p) for p in project},
              'source_bundle_sha256': digest(bundle), 'frameworks': {},
              'runtime_executed': False, 'device_execution_verified': False}

    def save():
        (artifacts / 'm3-art-guest-link.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    save()
    if sys.platform == 'darwin':
        for target in ('macos', 'ios'):
            _, details = prepare(output / 'pack', output / target, target, *art_notice, notices,
                                  name='ARTBoxRuntime', notice_name='ART-NOTICE.txt')
            report['frameworks'][target] = details
            save()
    print('Full ART guest linked from 462 objects; eight explicit VM/TLS host imports; ' +
          ('Mac/iOS 15 frameworks signed and verified; execution pending' if sys.platform == 'darwin'
           else 'Apple packaging and execution pending'))


if __name__ == '__main__': main()
