"""Compare real AOSP reference types in signed ARM64 code and native Linux."""
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
from environment import ROOT, environment
from ndk import obtain as obtain_ndk, REVISION
from sources import obtain
from art_reference_adapt import adapt, SOURCE, SHA256
from bionic_adapt import inventory
from dynamic_bundle import prepare


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args, **kwargs):
    return subprocess.check_output([str(a) for a in args], **kwargs)


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def checked_execution(executable, arguments, log):
    result = subprocess.run([str(executable), *map(str, arguments)], capture_output=True, text=True,
                            encoding='utf-8', timeout=30)
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    result.check_returncode()
    return json.loads(result.stdout)


def build_reference(build, artifacts, args):
    source_names = ('art-references', 'libbase-references', 'fmtlib-references')
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    sources = {name: obtain(name) for name in source_names}
    art, libbase, fmt = (sources[n] for n in source_names)
    original = (art / SOURCE).read_bytes()
    changed = adapt(original)
    for invalid in (original + b'\n', changed):
        try:
            adapt(invalid)
        except RuntimeError:
            pass
        else:
            raise RuntimeError('Reference adaptation accepted changed or already adapted input')
    overlay = build / 'overlay'
    target = overlay / SOURCE
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(changed)
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    toolchain = ndk / 'toolchains/llvm/prebuilt' / host
    tools = toolchain / 'bin'
    suffix = '.exe' if os.name == 'nt' else ''
    clang = tools / ('clang++' + suffix)
    builtins = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))
    notices = {
        'ART-NOTICE.txt': (art / 'NOTICE', specs['art-references']['notice_sha256']),
        'LIBBASE-NOTICE.txt': (libbase / 'NOTICE', specs['libbase-references']['notice_sha256']),
        'FMT-LICENSE.txt': (fmt / 'LICENSE', specs['fmtlib-references']['notice_sha256']),
        'FMT-NOTICE.txt': (fmt / 'NOTICE', next(f['sha256'] for f in specs['fmtlib-references']['files'] if f['path'] == 'NOTICE')),
        'LIBCXX-NOTICE.txt': (toolchain / 'NOTICE', builtins['notice_sha256']),
    }
    for name, (path, expected) in notices.items():
        if digest(path) != expected:
            raise RuntimeError('Reference notice hash mismatch: ' + name)
        (build / name).write_bytes(path.read_bytes())
    project_sources = ['fixtures/art-references/check.cpp', 'fixtures/art-references/linux.c',
                       'fixtures/art-references/native_delete.cpp', 'CMakeLists.txt',
                       'fixtures/art-references/artbox_art_reference_bridge.h', 'fixtures/bionic-dynamic/image.ld',
                       'tests/native_art_references.c', 'scripts/art_reference_adapt.py',
                       'core/src/managed_reference.c', 'core/include/artbox/managed_reference.h']
    report = {'scope': 'Real AOSP reference types only; no ART runtime, GC or DEX execution',
              'project_commit': run('git', 'rev-parse', 'HEAD', text=True).strip(),
              'project_sources': {p: digest(ROOT / p) for p in project_sources},
              'sources': {n: specs[n] for n in source_names}, 'ndk_revision': REVISION,
              'compiler': run(clang, '--version', text=True).splitlines()[0],
              'adaptation': {'path': SOURCE, 'upstream_sha256': SHA256, 'adapted_sha256': digest(target),
                             'rejected_changed_inputs': 2},
              'notices': {n: h for n, (_, h) in notices.items()},
              'expected_cases_per_profile': 19, 'device_execution_verified': False, 'profiles': {}}
    for poison in (False, True):
        profile = 'poisoned' if poison else 'plain'
        directory = build / profile
        directory.mkdir(exist_ok=True)
        modes = {}
        for adapted in (False, True):
            mode = 'adapted' if adapted else 'original'
            obj, elf = directory / (mode + '.o'), directory / (mode + '.so')
            flags = ['--target=aarch64-linux-android28', '-std=c++17', '-O2', '-DNDEBUG', '-DART_PAGE_SIZE_AGNOSTIC',
                     '-fPIC', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-mbranch-protection=none',
                     '-ffixed-x18', '-ffixed-x27', '-ffixed-x28', '-mno-outline-atomics', '-march=armv8-a']
            if poison:
                flags += ['-DART_HEAP_POISONING']
            if adapted:
                flags += ['-I', overlay / 'runtime', '-I', ROOT / 'fixtures/art-references']
            flags += ['-I', art / 'runtime', '-I', art / 'libartbase', '-I', libbase / 'include', '-I', fmt / 'include']
            run(clang, *flags, '-c', ROOT / 'fixtures/art-references/check.cpp', '-o', obj)
            imports = {line.split()[0] for line in run(tools / ('llvm-nm' + suffix), '--undefined-only',
                                                       '--format=posix', obj, text=True).splitlines() if line.strip()}
            expected = {'artbox_art_reference_compress', 'artbox_art_reference_decompress'} if adapted else set()
            if imports != expected:
                raise RuntimeError(f'Unexpected reference object imports: {profile}/{mode}: {imports}')
            run(tools / ('ld.lld' + suffix), '-shared', '--hash-style=both', '--build-id=none',
                '-z', 'max-page-size=16384', '--pack-dyn-relocs=relr', '-soname', elf.name,
                '-T', ROOT / 'fixtures/bionic-dynamic/image.ld', obj, '-o', elf)
            disassembly = run(tools / ('llvm-objdump' + suffix), '-d', '--no-show-raw-insn', elf, text=True)
            boundary = inventory(disassembly)
            if not boundary['instruction_count'] or any(v for k, v in boundary.items() if k != 'instruction_count'):
                raise RuntimeError('ART reference fixture violates the native instruction boundary')
            atomics = {op: len(re.findall(r'\b' + op + r'\s+w', disassembly)) for op in ('ldar', 'stlr')}
            if not all(atomics.values()):
                raise RuntimeError('ART volatile heap loads/stores were removed from the fixture')
            (directory / (mode + '.disassembly.txt')).write_text(disassembly, encoding='utf-8')
            modes[mode] = {'object_sha256': digest(obj), 'elf_sha256': digest(elf),
                           'elf_bytes': elf.stat().st_size, 'imports': sorted(imports), 'inventory': boundary,
                           'atomic_instructions': atomics}
        run(sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', directory / 'adapted.so', directory / 'pack')
        if sys.platform == 'darwin':
            if platform.machine().lower() not in ('arm64', 'aarch64'):
                raise RuntimeError('Signed execution requires a native ARM64 Mac')
            modes['frameworks'] = {}
            for target_platform in ('macos', 'ios'):
                name = 'ARTBoxArtReferences' + ('Poisoned' if poison else 'Plain')
                binary, metadata = prepare(directory / 'pack', directory / target_platform, target_platform,
                                            *notices['ART-NOTICE.txt'],
                                            {k: v for k, v in notices.items() if k != 'ART-NOTICE.txt'},
                                            name=name, notice_name='ART-NOTICE.txt')
                modes['frameworks'][target_platform] = metadata
                if target_platform == 'macos':
                    runner = Path(os.environ['ARTBOX_BUILD_DIR']) / 'host/artbox_native_art_references'
                    native = checked_execution(runner, [binary, directory / 'adapted.so', profile], directory / 'native.log')
                    if native['cases'] != 19 or native['native_base'] < 2**32 or not native['cleanup'] or native['encoding'] != 'heap-relative':
                        raise RuntimeError('Signed ART reference fixture did not meet its high-address contract')
                    modes['native'] = native
        report['profiles'][profile] = modes
    if (art / SOURCE).read_bytes() != original:
        raise RuntimeError('ART original reference header was changed')
    save(artifacts / 'm3-art-references.json', report)
    print('AOSP reference contracts: both NDK profiles built; ' +
          ('38 signed native cases passed; iOS frameworks verified' if sys.platform == 'darwin' else 'native execution pending'))


def linux_reference(build, artifacts, args):
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        raise RuntimeError('The original reference comparison requires native Linux ARM64')
    evidence = args.evidence_root.resolve()
    report = json.loads((evidence / 'artifacts/m3-art-references.json').read_text(encoding='utf-8'))
    if report['project_commit'] != run('git', 'rev-parse', 'HEAD', text=True).strip():
        raise RuntimeError('Reference artifact is from a different project commit')
    for path, expected in report['project_sources'].items():
        if digest(ROOT / path) != expected:
            raise RuntimeError('Reference project source changed: ' + path)
    inputs = evidence / 'build/m3/art-references'
    runner = build / 'original-reference-oracle'
    run(args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        ROOT / 'fixtures/art-references/linux.c', '-ldl', '-o', runner)
    results = {}
    for profile in ('plain', 'poisoned'):
        source = report['profiles'][profile]
        for mode in ('original', 'adapted'):
            for extension, key in (('.so', 'elf_sha256'), ('.o', 'object_sha256')):
                if digest(inputs / profile / (mode + extension)) != source[mode][key]:
                    raise RuntimeError('Reference input hash mismatch: ' + profile + '/' + mode)
        native = checked_execution(runner, [inputs / profile / 'original.so', profile], build / (profile + '.log'))
        if native['cases'] != 19 or native['cases'] != source['native']['cases'] or not native['cleanup'] or \
                native['encoding'] != 'absolute' or not 0 < native['native_base'] < 2**32:
            raise RuntimeError('Original and signed ART reference results disagree')
        results[profile] = {'native': native, 'original_elf_sha256': source['original']['elf_sha256']}
    save(artifacts / 'm3-art-references-linux.json', {
        'scope': report['scope'], 'project_commit': report['project_commit'], 'profiles': results,
        'mac_reference_sha256': digest(evidence / 'artifacts/m3-art-references.json')})
    print('38 original AOSP reference cases on native Linux agree with signed heap-relative references')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--evidence-root', type=Path)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()
    os.environ.update(environment())
    build = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/art-references'
    build.mkdir(parents=True, exist_ok=True)
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    (linux_reference if args.evidence_root else build_reference)(build, artifacts, args)


if __name__ == '__main__':
    main()
