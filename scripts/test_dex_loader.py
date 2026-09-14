"""Build pinned AOSP DEX loading support and check an original data fixture.

Windows compiles the Android ART units as a prerequisite. macOS executes the
native source build and signs an iOS framework; Linux runs the same verifier.
This command never starts ART or interprets DEX instructions.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import subprocess
from environment import ROOT, environment
from sources import obtain
from ndk import obtain as obtain_ndk, REVISION
from dex_fixture import make_hello, malformed_inputs
from test_low_address import macho

DEX_UNITS = ('compact_dex_file compact_offset_table descriptors_names dex_file dex_file_exception_helpers '
             'dex_file_layout dex_file_loader dex_file_tracking_registrar dex_file_verifier dex_instruction '
             'modifiers primitive signature standard_dex_file type_lookup_table utf').split()
BASE_UNITS = ('allocator file_magic globals_unix logging mem_map mem_map_unix memory_region os_linux '
              'utils time_utils zip_archive unix_file/fd_file unix_file/random_access_file_utils').split()
LIBBASE_UNITS = ('logging stringprintf strings threads posix_strerror_r file errors_unix mapped_file '
                 'parsebool properties').split()
LOG_UNITS = ('logger_name logger_write properties').split()
ZIP_UNITS = ['zip_archive.cc', 'zip_archive_stream_entry.cc', 'zip_cd_entry_map.cc', 'zip_error.cpp']


def run(*args, **kwargs):
    return subprocess.check_output([str(a) for a in args], **kwargs)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def time_source(art, build):
    """Supply declarations before the pinned time header uses them (libstdc++)."""
    relative = 'libartbase/base/time_utils.cc'
    source = art / relative
    expected = 'b847b3b46fd799c0f8f75d02c6d2ee8a7daa8e7203825dcc0b2d6d65dba1fab8'
    if digest(source) != expected:
        raise RuntimeError('ART time source differs from the reviewed upstream input')
    original = source.read_text(encoding='utf-8')
    before = '#include "time_utils.h"'
    if original.count(before) != 1 or original.count('#include <limits>\n') != 1:
        raise RuntimeError('ART time include context is missing or ambiguous')
    changed = original.replace('#include <limits>\n', '', 1).replace(
        before, '// ARTBox: declare std::min and numeric_limits before time_utils.h.\n'
        '#include <algorithm>\n#include <limits>\n\n' + before, 1)
    output = build / 'overlay' / relative
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(changed.encode('utf-8'))
    return output, {'path': relative, 'upstream_sha256': expected, 'adapted_sha256': digest(output)}


def compile_units(compiler, flags, units, directory, jobs):
    directory.mkdir(parents=True, exist_ok=True)
    def compile_one(item):
        name, source = item
        obj, log = directory / (name + '.o'), directory / (name + '.log')
        command = [str(a) for a in [*compiler, *flags, '-c', source, '-o', obj]]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            print(result.stderr, file=sys.stderr, flush=True)
            raise RuntimeError('DEX source compilation failed: ' + name)
        return obj, {'unit': name, 'source_sha256': digest(source), 'object_sha256': digest(obj)}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(compile_one, units))
    return [p for p, _ in results], [m for _, m in results]


def native_cases(executable, directory, inputs):
    results = {}
    for name, path in inputs.items():
        process = subprocess.run([str(executable), str(path)], capture_output=True, text=True,
                                 encoding='utf-8', timeout=30)
        (directory / (name + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        expected = 0 if name == 'hello' else 2
        if process.returncode != expected:
            raise RuntimeError(f'AOSP DEX case {name} returned {process.returncode}, expected {expected}')
        result = json.loads(process.stdout)
        if result != {'dex_verified': name == 'hello', 'dex_executed': False, 'result': expected}:
            raise RuntimeError('Unexpected DEX result: ' + name)
        if name != 'hello' and 'AOSP DEX verifier:' not in process.stderr:
            raise RuntimeError('Malformed DEX did not reach the AOSP loader/verifier: ' + name)
        results[name] = result
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--evidence-root', type=Path)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8:
        parser.error('--jobs must be between 1 and 8')
    os.environ.update(environment())
    build = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/dex-loader'
    build.mkdir(parents=True, exist_ok=True)
    names = ('art-dex', 'libbase-dex', 'liblog-dex', 'ziparchive-dex', 'jni-dex', 'fmtlib-references', 'property-info')
    sources = {n: obtain(n) for n in names}
    art, base, log, zip_source, jni, fmt, ids = (sources[n] for n in names)
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    data, layout = make_hello()
    all_data = {'hello': data, **malformed_inputs(data, layout)}
    inputs = {}
    for name, contents in all_data.items():
        inputs[name] = build / (name + '.dex')
        inputs[name].write_bytes(contents)
    record = {'scope': 'AOSP DEX loading/verification only; no ART startup or DEX execution',
              'platform': sys.platform, 'architecture': platform.machine(),
              'project_commit': run('git', 'rev-parse', 'HEAD', text=True).strip(),
              'sources': {n: specs[n] for n in names}, 'fixture': layout,
              'inputs': {n: digest(p) for n, p in inputs.items()},
              'project_sources': {p: digest(ROOT / p) for p in ('fixtures/art-dex/check.cpp', 'scripts/dex_fixture.py',
                                                              'scripts/test_dex_loader.py')},
              'native_execution': False, 'device_execution_verified': False}
    time_unit, record['time_include_adaptation'] = time_source(art, build)
    if args.evidence_root:
        reference = json.loads((args.evidence_root / 'artifacts/m3-dex-loader.json').read_text(encoding='utf-8'))
        if any(record[key] != reference[key] for key in ('project_commit', 'inputs', 'sources', 'project_sources',
                                                        'time_include_adaptation')):
            raise RuntimeError('DEX reference provenance or generated inputs differ')
    units = [('dex-' + n, art / 'libdexfile/dex' / (n + '.cc')) for n in DEX_UNITS]
    units += [('artbase-' + n.replace('/', '-'), art / 'libartbase/base' / (n + '.cc')) for n in BASE_UNITS]
    units = [(name, time_unit if name == 'artbase-time_utils' else path) for name, path in units]
    generator_headers = ['dex_file.h', 'dex_file_layout.h', 'dex_instruction.h', 'dex_instruction_utils.h', 'invoke_type.h']
    # Forward-slash paths also match the upstream generator's include-prefix logic on Windows.
    generated = run(sys.executable, '-B', art / 'tools/generate_operator_out.py', (art / 'libdexfile').as_posix(),
                    *[(art / 'libdexfile/dex' / h).as_posix() for h in generator_headers])
    enum_source = build / 'dex-operators.cc'
    enum_source.write_bytes(generated)
    units += [('dex-operators', enum_source)]
    record['generated_operators_sha256'] = digest(enum_source)
    include_paths = [art / 'libdexfile', art / 'libartbase', art / 'libartbase/base', art / 'libartpalette/include',
                     base / 'include', log / 'liblog/include', zip_source / 'include',
                     zip_source / 'incfs_support/include', fmt / 'include', jni / 'include_jni', ids / 'libcutils/include']
    flags = ['-std=c++20', '-O2', '-DNDEBUG', '-DART_PAGE_SIZE_AGNOSTIC', '-DSTATIC_LIB', '-DART_STATIC_LIBARTBASE',
             '-DART_BASE_ADDRESS=0x70000000', '-DFMT_HEADER_ONLY', '-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE64_SOURCE',
             '-DZIPARCHIVE_DISABLE_CALLBACK_API=1', '-DINCFS_SUPPORT_DISABLED=1', '-DZLIB_CONST',
             '-fPIC', '-fno-exceptions', '-fno-rtti', '-ffunction-sections', '-fdata-sections']
    for path in include_paths:
        flags += ['-I', path]
    notices = {title: (sources[name] / specs[name]['notice']) for title, name in (
        ('ART-NOTICE.txt', 'art-dex'), ('LIBBASE-NOTICE.txt', 'libbase-dex'), ('LIBLOG-NOTICE.txt', 'liblog-dex'),
        ('ZIPARCHIVE-SOURCE-NOTICE.txt', 'ziparchive-dex'), ('JNI-NOTICE.txt', 'jni-dex'),
        ('FMT-LICENSE.txt', 'fmtlib-references'), ('CUTILS-NOTICE.txt', 'property-info'))}
    notices['FMT-NOTICE.txt'] = fmt / 'NOTICE'
    notice_dir = build / 'notices'
    notice_dir.mkdir(exist_ok=True)
    for name, path in notices.items():
        (notice_dir / name).write_bytes(path.read_bytes())
    record['notices'] = {n: digest(p) for n, p in notices.items()}
    if sys.platform == 'win32':
        ndk = obtain_ndk(args.ndk_root)
        toolchain = ndk / 'toolchains/llvm/prebuilt/windows-x86_64'
        compiler = [toolchain / 'bin/clang++.exe', '--target=aarch64-linux-android35', '-ffixed-x18',
                    '-ffixed-x27', '-ffixed-x28', '-mno-outline-atomics', '-march=armv8-a']
        _, metadata = compile_units(compiler, flags, units + [('check', ROOT / 'fixtures/art-dex/check.cpp')],
                                    build / 'android-objects', args.jobs)
        record.update(android_units=metadata, ndk_revision=REVISION)
        notice = toolchain / 'NOTICE'
        expected = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
        if digest(notice) != expected:
            raise RuntimeError('NDK DEX object notice differs from the reviewed LLVM license')
        (notice_dir / 'LLVM-NOTICE.txt').write_bytes(notice.read_bytes())
        record['notices']['LLVM-NOTICE.txt'] = expected
    else:
        if sys.platform not in ('darwin', 'linux'):
            raise RuntimeError('Native DEX baseline currently supports macOS and Linux')
        units += [('libbase-' + n, base / (n + '.cpp')) for n in LIBBASE_UNITS]
        units += [('liblog-' + n, log / 'liblog' / (n + '.cpp')) for n in LOG_UNITS]
        units += [('zip-' + Path(n).stem, zip_source / n) for n in ZIP_UNITS]
        flags += ['-DLIBLOG_LOG_TAG=1006', '-DSNET_EVENT_LOG_TAG=1397638484', '-DANDROID_DEBUGGABLE=0']
        targets = ('macos', 'ios') if sys.platform == 'darwin' else ('linux',)
        record['targets'] = {}
        for target in targets:
            directory = build / target
            directory.mkdir(exist_ok=True)
            target_flags = []
            if target in ('macos', 'ios'):
                if platform.machine().lower() not in ('arm64', 'aarch64'):
                    raise RuntimeError('Apple DEX execution requires a native ARM64 Mac')
                sdk = 'iphoneos' if target == 'ios' else 'macosx'
                triple = 'arm64-apple-ios15.0' if target == 'ios' else 'arm64-apple-macos11.0'
                compiler = ['xcrun', '--sdk', sdk, 'clang++']
                target_flags = ['-target', triple, '-isysroot', run('xcrun', '--sdk', sdk, '--show-sdk-path').decode().strip()]
            else:
                compiler = [args.compiler]
            objects, metadata = compile_units(compiler, target_flags + flags, units, directory / 'objects', args.jobs)
            target_record = {'objects': metadata}
            if target == 'ios':
                name = 'ARTBoxDexLoader'
                framework = directory / (name + '.framework')
                framework.mkdir(exist_ok=True)
                binary = framework / name
                check, check_meta = compile_units(compiler, target_flags + flags,
                                                  [('check', ROOT / 'fixtures/art-dex/check.cpp')], directory / 'objects', 1)
                target_record['objects'] += check_meta
                run(*compiler, *target_flags, '-dynamiclib', *objects, *check, '-lz',
                    '-Wl,-dead_strip', '-Wl,-exported_symbol,_artbox_dex_inspect',
                    '-Wl,-install_name,@rpath/ARTBoxDexLoader.framework/ARTBoxDexLoader', '-o', binary)
                info = {'CFBundleIdentifier': 'org.artbox.DexLoader', 'CFBundleExecutable': name,
                        'CFBundleName': name, 'CFBundlePackageType': 'FMWK', 'CFBundleVersion': '1',
                        'CFBundleShortVersionString': '1.0', 'MinimumOSVersion': '15.0',
                        'CFBundleSupportedPlatforms': ['iPhoneOS']}
                (framework / 'Info.plist').write_bytes(plistlib.dumps(info))
                for notice in notice_dir.glob('*.txt'):
                    (framework / notice.name).write_bytes(notice.read_bytes())
                run('codesign', '--force', '--sign', '-', '--timestamp=none', framework)
                target_record['signed_binary'] = macho(binary, 2, None, kind=6)
            else:
                executable = directory / 'dex-loader-check'
                check, check_meta = compile_units(compiler, target_flags + flags + ['-DARTBOX_DEX_EXECUTABLE'],
                                                  [('check', ROOT / 'fixtures/art-dex/check.cpp')], directory / 'objects', 1)
                target_record['objects'] += check_meta
                run(*compiler, *target_flags, *objects, *check, '-lz', '-pthread',
                    '-Wl,-dead_strip' if target == 'macos' else '-Wl,--gc-sections', '-o', executable)
                if target == 'macos':
                    run('codesign', '--force', '--sign', '-', '--timestamp=none', executable)
                    target_record['signed_binary'] = macho(executable, 1, 2**32)
                    if target_record['signed_binary']['cpu'] != 0x100000c:
                        raise RuntimeError('DEX Mac executable is not native ARM64')
                target_record['cases'] = native_cases(executable, directory, inputs)
                record['native_execution'] = True
            record['targets'][target] = target_record
    suffix = '-linux' if args.evidence_root else ''
    save(Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / ('m3-dex-loader' + suffix + '.json'), record)
    print('DEX loader: ' + ('native format checks passed; DEX execution remains false' if record['native_execution']
                            else 'Android source units compiled; native loading/execution not tested'))


if __name__ == '__main__':
    main()
