"""Compare actual ART argument copying in signed high-address code and native Linux.

This compiles interpreter_common.cc; it does not start the interpreter or a collector.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess

from environment import ROOT, environment
from sources import obtain, obtain_files
from ndk import obtain as obtain_ndk, REVISION
from art_storage_adapt import adapt_storage
from bionic_adapt import inventory
from dynamic_bundle import prepare
from build_art_classlib import write_zip
from test_art_references import digest, run, save, checked_execution


PROJECT_INPUTS = (
    'LICENSE', 'THIRD_PARTY.md', 'CMakeLists.txt', 'docs/m3-interpreter-arguments.md',
    'fixtures/art-references/stack.cpp', 'fixtures/art-references/linux.c',
    'fixtures/art-references/native_delete.cpp', 'fixtures/art-references/artbox_art_reference_bridge.h',
    'fixtures/bionic-dynamic/image.ld', 'tests/native_art_references.c',
    'core/src/managed_reference.c', 'core/include/artbox/managed_reference.h',
    'scripts/test_art_stack.py', 'scripts/test_art_references.py', 'scripts/art_storage_adapt.py',
    'scripts/art_host_adapt.py', 'scripts/art_reference_adapt.py', 'scripts/environment.py',
    'scripts/sources.py', 'scripts/ndk.py', 'scripts/build_art_classlib.py',
    'scripts/bionic_adapt.py', 'scripts/dynamic_bundle.py', 'tools/wrap_dynamic.py',
    'third_party/sources.json', 'third_party/art/runtime-sources.json',
    'third_party/art/interpreter-arguments-boundary.json', 'third_party/bionic/builtins.json',
)


def build_contract(build, artifacts, args):
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    specs.update(json.loads((ROOT / 'third_party/art/runtime-sources.json').read_text(encoding='utf-8')))
    cache = Path(os.environ['ARTBOX_CACHE_DIR'])
    sources = {name: obtain_files(name, specs[name], cache)
               for name in ('art-runtime', 'art-tinyxml2', 'art-dlmalloc')}
    sources.update({name: obtain(name) for name in ('libbase-dex', 'fmtlib-references', 'jni-dex', 'bionic')})
    art = sources['art-runtime']
    boundary = json.loads((ROOT / 'third_party/art/interpreter-arguments-boundary.json').read_text(encoding='utf-8'))
    reference_only, adapted = build / 'reference-source', build / 'adapted-source'
    changes = {
        'reference': adapt_storage(art, reference_only, specs['art-runtime']['files'], {'files': []}),
        'adapted': adapt_storage(art, adapted, specs['art-runtime']['files'], boundary),
    }
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    toolchain = ndk / 'toolchains/llvm/prebuilt' / host
    tools = toolchain / 'bin'
    suffix = '.exe' if os.name == 'nt' else ''
    clang = tools / ('clang++' + suffix)
    exports = build / 'exports.map'
    exports.write_text('{ global: artbox_art_stack_check; local: *; };\n', encoding='utf-8')
    notices, source_files = {}, {'artbox/' + name: ROOT / name for name in PROJECT_INPUTS}
    for name, source in sources.items():
        spec = specs[name]
        notice = source / spec['notice']
        if digest(notice) != spec['notice_sha256']: raise RuntimeError('Stack source notice differs: ' + name)
        notices[name + '-NOTICE.txt'] = (notice, spec['notice_sha256'])
        selected = ['libc/platform/bionic/tls.h', 'libc/platform/bionic/tls_defines.h', spec['notice']] \
            if name == 'bionic' else [item['path'] for item in spec['files']]
        source_files.update({'upstream/' + name + '/' + path: source / path for path in selected})
    builtins = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))
    if digest(toolchain / 'NOTICE') != builtins['notice_sha256']:
        raise RuntimeError('NDK notice differs from its reviewed pin')
    notices['NDK-NOTICE.txt'] = (toolchain / 'NOTICE', builtins['notice_sha256'])
    for mode, tree in (('reference', reference_only), ('adapted', adapted)):
        source_files.update({mode + '/' + item['path']: tree / item['path'] for item in changes[mode]})
    source_files.update({'notices/' + name: path for name, (path, _) in notices.items()})
    source_files['generated/exports.map'] = exports
    source_bundle = build / 'corresponding-source.zip'
    write_zip(source_bundle, source_files)
    report = {'scope': 'Actual ART ShadowFrame, AssignRegister and CopyRegisters; no VM or collector execution',
              'project_commit': run('git', 'rev-parse', 'HEAD', text=True).strip(),
              'project_sources': {name: digest(ROOT / name) for name in PROJECT_INPUTS},
              'sources': {name: specs[name] for name in sources}, 'adaptations': changes,
              'source_bundle_sha256': digest(source_bundle), 'exports_sha256': digest(exports),
              'notices': {name: expected for name, (_, expected) in notices.items()},
              'ndk_revision': REVISION, 'compiler': run(clang, '--version', text=True).splitlines()[0],
              'expected_cases_per_profile': 18, 'runtime_executed': False,
              'device_execution_verified': False, 'profiles': {}}
    base = ['--target=aarch64-linux-android35', '-U__ANDROID__', '-std=c++20', '-O2', '-DNDEBUG',
            '-fPIC', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-mbranch-protection=none',
            '-ffixed-x18', '-ffixed-x27', '-ffixed-x28', '-mno-outline-atomics', '-march=armv8-a',
            '-ftrivial-auto-var-init=zero', '-Wno-invalid-offsetof',
            '-ffunction-sections', '-fdata-sections', '-fvisibility=hidden',
            '-DART_PAGE_SIZE_AGNOSTIC', '-DART_DEFAULT_GC_TYPE_IS_SS', '-DART_USE_TLAB=1',
            '-DUSE_D8_DESUGAR=1', '-DFMT_HEADER_ONLY', '-DART_FRAME_SIZE_LIMIT=1736',
            '-DART_BASE_ADDRESS=0x70000000', '-DART_BASE_ADDRESS_MIN_DELTA=(-0x1000000)',
            '-DART_BASE_ADDRESS_MAX_DELTA=0x1000000', '-DART_CLANG_PATH="clang"']
    base += ['-DART_STACK_OVERFLOW_GAP_' + arch + '=8192' for arch in ('arm', 'arm64', 'riscv64', 'x86', 'x86_64')]
    for poison in (False, True):
        profile = 'poisoned' if poison else 'plain'
        directory = build / profile
        directory.mkdir(exist_ok=True)
        modes = {}
        for mode, selected in (('original', art), ('reference', reference_only), ('adapted', adapted)):
            obj, elf = directory / (mode + '.o'), directory / (mode + '.so')
            paths = [selected / name for name in ('runtime', 'libartbase', 'libdexfile', 'libprofile',
                     'libelffile', 'libartpalette/include', 'libnativeloader/include', 'libnativebridge/include')]
            paths += [sources['libbase-dex'] / 'include', sources['fmtlib-references'] / 'include',
                      sources['jni-dex'] / 'include_jni', sources['art-tinyxml2'], sources['art-dlmalloc'],
                      sources['bionic'] / 'libc/platform', ROOT / 'fixtures/art-references']
            flags = [*base, *(flag for path in paths for flag in ('-I', str(path)))]
            if poison: flags.append('-DART_HEAP_POISONING')
            command = [clang, *flags, '-c', ROOT / 'fixtures/art-references/stack.cpp', '-o', obj]
            result = subprocess.run(list(map(str, command)), capture_output=True, text=True, encoding='utf-8')
            (directory / (mode + '-build.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
            if result.returncode: raise RuntimeError('Stack compilation failed; see ' + str(directory / (mode + '-build.log')))
            run(tools / ('ld.lld' + suffix), '-shared', '--gc-sections', '--version-script=' + str(exports),
                '--hash-style=both', '--build-id=none', '-z', 'max-page-size=16384', '--pack-dyn-relocs=relr',
                '-soname', elf.name, '-T', ROOT / 'fixtures/bionic-dynamic/image.ld', obj, '-o', elf)
            # The full translation unit compiles. Only the exported test and its reachable helpers
            # remain in the DSO, so inspect imports and instructions after section collection.
            imports = {line.split()[0] for line in run(tools / ('llvm-nm' + suffix), '--undefined-only',
                       '--format=posix', elf, text=True).splitlines() if line.strip()}
            expected = {'_ZdlPv'}
            if mode != 'original': expected |= {'artbox_art_reference_compress', 'artbox_art_reference_decompress'}
            if imports != expected: raise RuntimeError('Unexpected stack payload imports: ' + repr(imports))
            disassembly = run(tools / ('llvm-objdump' + suffix), '-d', '--no-show-raw-insn', elf, text=True)
            instructions = inventory(disassembly)
            if not instructions['instruction_count'] or any(v for k, v in instructions.items() if k != 'instruction_count'):
                raise RuntimeError('Stack payload violates the native instruction boundary')
            (directory / (mode + '.disassembly.txt')).write_text(disassembly, encoding='utf-8')
            modes[mode] = {'object_sha256': digest(obj), 'elf_sha256': digest(elf), 'elf_bytes': elf.stat().st_size,
                           'imports': sorted(imports), 'command': list(map(str, command)), 'inventory': instructions}
            pack = directory / (mode + '-pack')
            run(sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', elf, pack)
            if sys.platform == 'darwin' and mode != 'original':
                if platform.machine().lower() not in ('arm64', 'aarch64'):
                    raise RuntimeError('Signed execution requires native ARM64 macOS')
                modes[mode]['frameworks'] = {}
                for target in (('macos', 'ios') if mode == 'adapted' else ('macos',)):
                    name = 'ARTBoxArtStack' + mode.title() + profile.title()
                    binary, metadata = prepare(pack, directory / (mode + '-' + target), target,
                        *notices['art-runtime-NOTICE.txt'],
                        {k: v for k, v in notices.items() if k != 'art-runtime-NOTICE.txt'},
                        name=name, notice_name='art-runtime-NOTICE.txt')
                    modes[mode]['frameworks'][target] = metadata
                    if target == 'macos':
                        runner = Path(os.environ['ARTBOX_BUILD_DIR']) / 'host/artbox_native_art_references'
                        rejection = mode == 'reference'
                        native = checked_execution(runner, [binary, elf, profile,
                            'stack-negative' if rejection else 'stack'], directory / (mode + '-native.log'))
                        if (native['result'] != (-4 if rejection else 18) or native['native_base'] < 2**32 or
                                not native['cleanup'] or native['expected_rejection'] != rejection):
                            raise RuntimeError('Signed stack contract disagrees with its argument representation')
                        modes[mode]['native'] = native
        report['profiles'][profile] = modes
    save(artifacts / 'm3-art-stack.json', report)
    print('ART argument copying: six ARM64 payloads built; ' +
          ('36 signed native cases and two reference-only negative controls passed' if sys.platform == 'darwin'
           else 'native execution pending'))


def linux_contract(build, artifacts, args):
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        raise RuntimeError('Original argument copying requires native Linux ARM64')
    evidence = args.evidence_root.resolve()
    report = json.loads((evidence / 'artifacts/m3-art-stack.json').read_text(encoding='utf-8'))
    if report['project_commit'] != run('git', 'rev-parse', 'HEAD', text=True).strip():
        raise RuntimeError('Stack artifact belongs to a different revision')
    for path, expected in report['project_sources'].items():
        if digest(ROOT / path) != expected: raise RuntimeError('Stack project input changed: ' + path)
    inputs = evidence / 'build/m3/art-stack'
    if digest(inputs / 'corresponding-source.zip') != report['source_bundle_sha256']:
        raise RuntimeError('Stack corresponding source changed')
    runner, driver, deleter = build / 'original-stack-oracle', build / 'driver.o', build / 'native-delete.o'
    run(args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-c', ROOT / 'fixtures/art-references/linux.c', '-o', driver)
    run(args.cxx, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-c', ROOT / 'fixtures/art-references/native_delete.cpp', '-o', deleter)
    run(args.cxx, driver, deleter, '-ldl', '-o', runner)
    results = {}
    for profile in ('plain', 'poisoned'):
        source = report['profiles'][profile]
        for mode in ('original', 'reference', 'adapted'):
            for extension, key in (('.o', 'object_sha256'), ('.so', 'elf_sha256')):
                if digest(inputs / profile / (mode + extension)) != source[mode][key]:
                    raise RuntimeError('Stack native input changed')
        native = checked_execution(runner, [inputs / profile / 'original.so', profile, 'stack'], build / (profile + '.log'))
        if (native['cases'] != 18 or native['cases'] != source['adapted']['native']['cases'] or
                not native['cleanup'] or native['encoding'] != 'absolute' or not 0 < native['native_base'] < 2**32):
            raise RuntimeError('Original Linux and signed stack contracts disagree')
        results[profile] = {'native': native, 'original_elf_sha256': source['original']['elf_sha256']}
    save(artifacts / 'm3-art-stack-linux.json', {'scope': report['scope'], 'project_commit': report['project_commit'],
         'profiles': results, 'mac_evidence_sha256': digest(evidence / 'artifacts/m3-art-stack.json')})
    print('36 original ART argument-copy cases passed on native Linux ARM64')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--evidence-root', type=Path)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'clang'))
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    args = parser.parse_args()
    os.environ.update(environment())
    build = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/art-stack'
    build.mkdir(parents=True, exist_ok=True)
    (linux_contract if args.evidence_root else build_contract)(build, Path(os.environ['ARTBOX_ARTIFACTS_DIR']), args)


if __name__ == '__main__': main()
