"""Build pinned libcore JNI libraries and test their native dependencies.

Requires an ART runtime build in the same workspace. Does not start a Java VM.
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
import shutil
import subprocess
import time
import zipfile

from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from sources import obtain, obtain_files


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def run(command, log):
    result = subprocess.run(list(map(str, command)), capture_output=True, encoding='utf-8')
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout + result.stderr, flush=True)
        raise RuntimeError(f'Command failed; see {log}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['android', 'linux'], default='linux' if sys.platform == 'linux' else 'android')
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--link', action='store_true')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--runtime-dir', type=Path)
    parser.add_argument('--native-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'clang'))
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--ar', default=os.environ.get('AR', 'ar'))
    args = parser.parse_args()
    if args.jobs < 1: parser.error('--jobs must be positive')
    if args.link and (not args.all or args.profile != 'linux'):
        parser.error('--link requires --all --profile linux')
    if args.profile == 'linux' and (sys.platform != 'linux' or platform.machine().lower() not in ['aarch64', 'arm64']):
        parser.error('The Linux reference requires native ARM64 Linux')
    os.environ.update(environment())
    build = Path(os.environ['ARTBOX_BUILD_DIR'])
    output = (args.build_dir or build / 'm3/libcore-native' / args.profile).resolve()
    runtime = (args.runtime_dir or build / 'm3/runtime-build' / args.profile).resolve()
    native = (args.native_dir or build / 'm3/native-libraries' / args.profile).resolve()
    output.mkdir(parents=True, exist_ok=True)
    metadata = runtime / 'all-results.json'
    if not metadata.exists() and not args.link: metadata = runtime / 'preflight-results.json'
    runtime_record = json.loads(metadata.read_text(encoding='utf-8'))
    if runtime_record['profile'] != args.profile or any(r['exit'] for r in runtime_record['results']):
        raise RuntimeError('Build the matching ART runtime profile first')
    runtime_bundle = runtime / 'corresponding-source.zip'
    if digest(runtime_bundle) != runtime_record['source_bundle_sha256']:
        raise RuntimeError('Runtime source bundle changed')
    # Reuse the actual ART header configuration, including checked host edits.
    # These recorded include paths must still exist in this build workspace.
    command = next(r['command'] for r in runtime_record['results'] if r['unit'] == 'runtime-app_info.cc')
    jvm_flags = [x for x in command[1:command.index('-c')] if x != '-DBUILDING_LIBART']
    for index, word in enumerate(jvm_flags):
        if word == '-I' and not Path(jvm_flags[index + 1]).is_dir():
            raise RuntimeError('Runtime include paths moved; rebuild ART in this workspace')

    catalog_path = ROOT / 'third_party/art/libcore-native-sources.json'
    graph_path = ROOT / 'third_party/art/libcore-native.json'
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    graph = json.loads(graph_path.read_text(encoding='utf-8'))
    cache = Path(os.environ['ARTBOX_CACHE_DIR'])
    sources = {name: obtain_files(name, spec, cache) for name, spec in catalog.items()}
    for filename, names in [('native-library-sources.json', ['art-nativehelper-runtime', 'art-icu-native']),
                             ('runtime-sources.json', ['art-zlib'])]:
        specs = json.loads((ROOT / 'third_party/art' / filename).read_text(encoding='utf-8'))
        for name in names:
            catalog[name] = specs[name]
            sources[name] = obtain_files(name, specs[name], cache)
    other = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    for name in ['libbase-dex', 'liblog-dex', 'fmtlib-references', 'ziparchive-dex', 'property-info']:
        catalog[name] = other[name]
        sources[name] = obtain(name)

    # Preserve upstream's relative sibling includes without changing its sources.
    layout = output / 'source-layout'
    layout_files = {}
    for name, directory in [('art-libcore-native', 'libcore'), ('art-openjdkjvm', 'art')]:
        for item in catalog[name]['files']:
            target = layout / directory / item['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sources[name] / item['path'], target)
            layout_files[target.relative_to(output).as_posix()] = item['sha256']
    fd_header = layout / 'libcore/ojluni/src/external/fdlibm/fdlibm.h'
    fd_header.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(sources['art-fdlibm'] / 'fdlibm.h', fd_header)
    layout_files[fd_header.relative_to(output).as_posix()] = digest(fd_header)

    cc, cxx, ar = args.cc, args.cxx, args.ar
    abi, toolchain = [], None
    if args.profile == 'android':
        ndk = obtain_ndk(args.ndk_root)
        host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
        toolchain = ndk / 'toolchains/llvm/prebuilt' / host
        suffix = '.exe' if os.name == 'nt' else ''
        cc, cxx, ar = [str(toolchain / 'bin' / (name + suffix)) for name in ['clang', 'clang++', 'llvm-ar']]
        abi = ['--target=aarch64-linux-android35', '-U__ANDROID__']
    archiver_version = run([ar, '--version'], output / 'archiver-version.log') if args.link else None
    base = [*abi, '-O1', '-DNDEBUG', '-fPIC', '-march=armv8-a', '-mno-outline-atomics',
            '-ffixed-x18', '-ffixed-x27', '-ffixed-x28']
    helper, icu, core = sources['art-nativehelper-runtime'], sources['art-icu-native'], layout / 'libcore'
    paths = [helper / p for p in ['include', 'include_jni', 'include_platform', 'header_only_include', 'include_platform_header_only']]
    paths += [core / p for p in ['luni/src/main/native', 'ojluni/src/main/native']]
    paths += [icu / 'libicu/ndk_headers', sources['art-crypto-native'] / 'src/include',
              sources['art-expat'] / 'expat/lib', sources['art-expat'], icu / 'icu4c/source/common']
    paths += [sources[name] / subdir for name, subdir in
              [('libbase-dex', 'include'), ('liblog-dex', 'liblog/include'), ('fmtlib-references', 'include'),
               ('ziparchive-dex', 'include'), ('ziparchive-dex', 'incfs_support/include'), ('property-info', 'libcutils/include')]]
    paths.append(sources['art-zlib'])
    if args.profile == 'linux': paths.append(sources['art-host-capability'] / 'libc/include')
    includes = [word for path in paths for word in ['-I', str(path)]]
    common = base + includes + ['-DFMT_HEADER_ONLY', '-D_LARGEFILE64_SOURCE', '-D_GNU_SOURCE',
                                '-DLINUX', '-D_FILE_OFFSET_BITS=64', '-DU_USING_ICU_NAMESPACE=0']
    cpp = ['-std=c++20', '-fno-exceptions', '-fno-rtti', '-DLIBICU_U_SHOW_CPLUSPLUS_API=1']
    units = []
    for group, names in graph.items():
        selected = names if args.all or group in ['androidio', 'jvm'] else names[:2]
        for name in selected:
            driver = cc
            if group in ['javacore', 'androidio', 'openjdk']:
                source = core / name
                is_cpp = source.suffix == '.cpp'
                flags = common + (cpp if is_cpp else ['-std=c11'])
                if is_cpp: driver = cxx
            elif group == 'jvm':
                source, flags, driver = layout / 'art' / name, jvm_flags + includes + ['-include', 'math.h'], cxx
            elif group == 'fdlibm':
                source = sources['art-fdlibm'] / name
                flags = base + ['-std=c99', '-D_IEEE_LIBM', '-D__LITTLE_ENDIAN', '-fno-strict-aliasing',
                                '-Wno-sign-compare', '-Wno-dangling-else', '-Wno-unknown-pragmas',
                                '-Wno-logical-op-parentheses', '-Wno-sometimes-uninitialized']
            elif group == 'expat':
                source = sources['art-expat'] / name
                flags = base + ['-std=c11', '-DHAVE_EXPAT_CONFIG_H', '-include', 'stdio.h', '-fno-strict-aliasing',
                                '-I', str(sources['art-expat']), '-I', str(sources['art-expat'] / 'expat/lib')]
            elif group == 'crypto':
                source = sources['art-crypto-native'] / name
                flags = base + ['-std=c11', '-DBORINGSSL_IMPLEMENTATION', '-DOPENSSL_NO_ASM',
                                '-I', str(sources['art-crypto-native'] / 'src/include')]
                if args.profile == 'linux': flags.append('-D_GNU_SOURCE')
            else: raise RuntimeError('Unknown libcore group: ' + group)
            units.append((group + '-' + source.name, source, flags, driver, group))
    if len(units) != (208 if args.all else 12) or len({x[0] for x in units}) != len(units):
        raise RuntimeError('Libcore source selection changed')
    if args.profile == 'linux':
        units.append(('host-capabilities', ROOT / 'platform/linux/capabilities.c',
                      [*base, '-std=c11', '-D_GNU_SOURCE', '-I', str(sources['art-host-capability'] / 'libc/include')],
                      cc, 'capabilities'))

    files = {'dependencies/art-runtime-corresponding-source.zip': runtime_bundle,
             'dependencies/art-runtime-build.json': metadata}
    notices = output / 'notices'
    notices.mkdir(exist_ok=True)
    for name, source in sources.items():
        for item in catalog[name]['files']:
            path = source / item['path']
            if digest(path) != item['sha256']: raise RuntimeError('Selected source changed: ' + str(path))
            files['upstream/' + name + '/' + item['path']] = path
        shutil.copyfile(source / catalog[name]['notice'], notices / (name + '.txt'))
    shutil.copyfile(ROOT / 'LICENSE', notices / 'ARTBOX-LICENSE.txt')
    if toolchain:
        expected = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
        if digest(toolchain / 'NOTICE') != expected: raise RuntimeError('NDK runtime notice changed')
        shutil.copyfile(toolchain / 'NOTICE', notices / 'NDK-NOTICE.txt')
    if args.link:
        native_record = json.loads((native / 'all-results.json').read_text(encoding='utf-8'))
        if native_record['profile'] != 'linux' or not native_record.get('native_check') or not runtime_record.get('link'):
            raise RuntimeError('Build and test the Linux runtime and native ICU dependencies first')
        if digest(native / 'corresponding-source.zip') != native_record['source_bundle_sha256']:
            raise RuntimeError('Native dependency source bundle changed')
        if digest(native / 'libart.so') != runtime_record['link']['artifacts']['libart.so']['sha256']:
            raise RuntimeError('The runtime and native dependencies use different ART builds')
        for name, item in native_record['linked_artifacts'].items():
            path = native / name
            if digest(path) != item['sha256']: raise RuntimeError('Native dependency changed: ' + name)
            if path.suffix == '.so': shutil.copyfile(path, output / name)
        files['dependencies/native-libraries-corresponding-source.zip'] = native / 'corresponding-source.zip'
        files['dependencies/native-libraries-build.json'] = native / 'all-results.json'
    project = ['LICENSE', 'THIRD_PARTY.md', 'docs/m3-libcore-native.md', 'third_party/sources.json',
               'third_party/art/libcore-native-sources.json', 'third_party/art/libcore-native.json',
               'third_party/art/native-library-sources.json', 'third_party/art/runtime-sources.json',
               'third_party/bionic/builtins.json', 'fixtures/art-runtime/native_libcore.cpp']
    project.append('platform/linux/capabilities.c')
    project += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / 'scripts').glob('*.py'))]
    for name in project: files['artbox/' + name] = ROOT / name
    for path in notices.iterdir(): files['notices/' + path.name] = path
    archive = output / 'corresponding-source.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            bundle.writestr(entry, path.read_bytes())
    record = {'profile': args.profile, 'runtime_executed': False,
              'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_bundle_sha256': digest(archive), 'source_layout': layout_files,
              'project_sources': {name: digest(ROOT / name) for name in project}, 'sources': catalog,
              'notices': {p.name: digest(p) for p in notices.iterdir()},
              'archiver_version': archiver_version,
              'compiler_version': run([cxx, '--version'], output / 'compiler-version.log')}
    save(output / 'build-inputs.json', record)
    objects = output / 'objects'
    objects.mkdir(exist_ok=True)
    def compile_one(unit):
        name, source, flags, driver, group = unit
        obj = objects / (name + '.o')
        command = list(map(str, [driver, *flags, '-c', source, '-o', obj]))
        start = time.monotonic()
        result = subprocess.run(command, capture_output=True, encoding='utf-8')
        obj.with_suffix('.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        entry = {'unit': name, 'group': group, 'command': command, 'exit': result.returncode,
                 'source_sha256': digest(source), 'seconds': time.monotonic() - start}
        if result.returncode: print(result.stderr, flush=True)
        else: entry['object_sha256'] = digest(obj)
        print(name, 'PASS' if result.returncode == 0 else 'FAIL', flush=True)
        return entry
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool: results = list(pool.map(compile_one, units))
    record['results'] = results
    result_path = output / ('all-results.json' if args.all else 'preflight-results.json')
    save(result_path, record)
    print('Passed', sum(x['exit'] == 0 for x in results), '/', len(results), flush=True)
    if any(x['exit'] for x in results): return 1
    if args.link:
        record['link_commands'] = []
        def link_run(command, log):
            record['link_commands'].append(list(map(str, command)))
            save(result_path, record)
            return run(command, log)
        def inputs(groups):
            paths = []
            for item in results:
                if item['group'] not in groups: continue
                obj = objects / (item['unit'] + '.o')
                if digest(obj) != item['object_sha256']: raise RuntimeError('Libcore object changed before link')
                paths.append(obj)
            return paths
        for group in ['fdlibm', 'crypto']:
            link_run([ar, 'rcs', output / ('lib' + group + '.a'), *inputs([group])], output / (group + '-archive.log'))
        def link(name, groups, dependencies, flags=()):
            link_run([cxx, '-shared', '-Wl,-z,defs', '-Wl,-soname,' + name, *flags, *inputs(groups),
                 *[output / name for name in dependencies], '-Wl,-rpath,$ORIGIN', '-pthread', '-ldl', '-lm',
                 '-o', output / name], output / (name + '-link.log'))
        link('libexpat.so', ['expat'], [])
        link('libandroidio.so', ['androidio'], ['libart.so'])
        link('libopenjdkjvm.so', ['jvm'], ['libart.so', 'libnativehelper.so'])
        shared = ['libandroidio.so', 'libicu.so', 'libnativehelper.so', 'libart.so', 'libcrypto.a']
        link('libjavacore.so', ['javacore', 'capabilities'], [*shared, 'libexpat.so'],
             ['-Wl,--version-script=' + str(sources['art-libcore-exports'] / 'libjavacore.map')])
        link('libopenjdk.so', ['openjdk'], [*shared, 'libopenjdkjvm.so', 'libfdlibm.a'])
        harness = output / 'native-libcore-check'
        link_run([cxx, *common, *cpp, '-I', sources['art-fdlibm'], ROOT / 'fixtures/art-runtime/native_libcore.cpp',
             *inputs(['capabilities']),
             output / 'libcrypto.a', output / 'libfdlibm.a', output / 'libexpat.so', output / 'libopenjdkjvm.so',
             '-Wl,-rpath,$ORIGIN', '-pthread', '-ldl', '-lm', '-o', harness], output / 'native-libcore-check-build.log')
        scratch = output / 'scratch'
        scratch.mkdir(exist_ok=True)
        start = time.monotonic()
        stdout = run([harness, scratch, output], output / 'native-libcore-check.log')
        record['native_check'] = {'stdout': stdout, 'seconds': time.monotonic() - start, 'runtime_executed': False}
        record['linked_artifacts'] = {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)}
                                      for p in [*sorted(output.glob('*.so')), *sorted(output.glob('*.a')), harness]}
        save(result_path, record)
        print(stdout, flush=True)
    return 0


if __name__ == '__main__': raise SystemExit(main())
