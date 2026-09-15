"""Build ART's nativehelper and ICU dependencies; this does not start ART."""
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

CATALOG = ROOT / 'third_party/art/native-library-sources.json'
GRAPH = ROOT / 'third_party/art/native-libraries.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def run(command, log, env=None):
    result = subprocess.run(list(map(str, command)), capture_output=True, encoding='utf-8', env=env)
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        print(result.stdout + result.stderr, flush=True)
        raise RuntimeError(f'Command failed; see {log}')
    return result.stdout


def source_bundle(output, sources, catalog, toolchain, runtime_dir):
    files = {}
    notices = output / 'notices'
    notices.mkdir(exist_ok=True)
    for name, source in sources.items():
        spec = catalog[name]
        for item in spec['files']:
            path = source / item['path']
            if digest(path) != item['sha256']: raise RuntimeError('Source changed: ' + str(path))
            files['upstream/' + name + '/' + item['path']] = path
        notice = source / spec['notice']
        if digest(notice) != spec['notice_sha256']: raise RuntimeError('Source notice changed: ' + name)
        shutil.copyfile(notice, notices / (name + '.txt'))
    shutil.copyfile(ROOT / 'LICENSE', notices / 'ARTBOX-LICENSE.txt')
    if toolchain:
        expected = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
        if digest(toolchain / 'NOTICE') != expected: raise RuntimeError('NDK runtime notice changed')
        shutil.copyfile(toolchain / 'NOTICE', notices / 'NDK-NOTICE.txt')
    project = ['LICENSE', 'THIRD_PARTY.md', 'docs/m3-native-libraries.md',
               'third_party/sources.json', 'third_party/bionic/builtins.json',
               'third_party/art/native-library-sources.json', 'third_party/art/native-libraries.json',
               'fixtures/art-runtime/native_icu.cpp']
    project += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / 'scripts').glob('*.py'))]
    for name in project: files['artbox/' + name] = ROOT / name
    dependency = None
    if runtime_dir:
        metadata = runtime_dir / 'all-results.json'
        record = json.loads(metadata.read_text(encoding='utf-8'))
        if record['profile'] != 'linux' or not record.get('link'):
            raise RuntimeError('Native libraries require the linked Linux ART reference')
        binary, bundle = runtime_dir / 'libart.so', runtime_dir / 'corresponding-source.zip'
        if (digest(binary) != record['link']['artifacts']['libart.so']['sha256'] or
                digest(bundle) != record['source_bundle_sha256']):
            raise RuntimeError('ART dependency differs from its build record')
        shutil.copyfile(binary, output / 'libart.so')
        files['dependencies/art-runtime-corresponding-source.zip'] = bundle
        files['dependencies/art-runtime-build.json'] = metadata
        dependency = {'libart_sha256': digest(binary), 'source_bundle_sha256': digest(bundle)}
    for path in notices.iterdir(): files['notices/' + path.name] = path
    archive = output / 'corresponding-source.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            bundle.writestr(entry, path.read_bytes())
    return {'source_bundle_sha256': digest(archive), 'runtime_dependency': dependency,
            'project_sources': {name: digest(ROOT / name) for name in project},
            'notices': {p.name: digest(p) for p in notices.iterdir()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['android', 'linux'], default='linux' if sys.platform == 'linux' else 'android')
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--link', action='store_true', help='Link native Linux dependencies and run their ICU check')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--runtime-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--cc', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()
    if args.jobs < 1: parser.error('--jobs must be positive')
    if args.link and (not args.all or args.profile != 'linux'):
        parser.error('--link requires --all --profile linux')
    if args.profile == 'linux' and (sys.platform != 'linux' or platform.machine().lower() not in ['arm64', 'aarch64']):
        parser.error('The Linux reference requires native ARM64 Linux')
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/native-libraries' / args.profile
    output.mkdir(parents=True, exist_ok=True)
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    graph = json.loads(GRAPH.read_text(encoding='utf-8'))
    cache = Path(os.environ['ARTBOX_CACHE_DIR'])
    sources = {name: obtain_files(name, spec, cache) for name, spec in catalog.items()}
    other = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    for name in ['libbase-dex', 'liblog-dex', 'fmtlib-references']:
        sources[name] = obtain(name)
        catalog[name] = other[name]
    abi = []
    toolchain = None
    cxx, cc = args.cxx, args.cc
    if args.profile == 'android':
        ndk = obtain_ndk(args.ndk_root)
        host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
        toolchain = ndk / 'toolchains/llvm/prebuilt' / host
        suffix = '.exe' if os.name == 'nt' else ''
        cxx, cc = [str(toolchain / 'bin' / (name + suffix)) for name in ['clang++', 'clang']]
        abi = ['--target=aarch64-linux-android35', '-U__ANDROID__']
    base = [*abi, '-O1', '-DNDEBUG', '-fPIC', '-march=armv8-a', '-mno-outline-atomics',
            '-ffixed-x18', '-ffixed-x27', '-ffixed-x28']
    cpp = ['-std=c++20', '-fno-exceptions']  # ICU's upstream build enables RTTI.
    helper, icu = sources['art-nativehelper-runtime'], sources['art-icu-native']
    paths = [helper / p for p in ['include', 'include_jni', 'header_only_include',
             'include_platform', 'include_platform_header_only']]
    paths += [sources['libbase-dex'] / 'include', sources['liblog-dex'] / 'liblog/include',
              sources['fmtlib-references'] / 'include']
    paths += [icu / p for p in ['icu4c/source/common', 'icu4c/source/i18n',
              'android_icu4c/include', 'libandroidicuinit/include']]
    common = [*base, '-DFMT_HEADER_ONLY', *[word for p in paths for word in ['-I', str(p)]]]
    units = []
    # JNIHelp.c uses the XSI int-returning strerror_r on this host profile.
    # Strict C11 hides that declaration unless POSIX interfaces are requested.
    helper_features = ['-D_POSIX_C_SOURCE=200809L'] if args.profile == 'linux' else []
    for name in graph['nativehelper']:
        units.append(('helper-' + name, helper / name, [*common, *helper_features, '-std=c11', '-fvisibility=protected'], cc, 'helper'))
    for group, names in graph['icu'].items():
        selected = names if args.all or group not in ['common', 'i18n'] else names[:2]
        flags = common + cpp + ['-DUCONFIG_USE_ML_PHRASE_BREAKING=1', '-DU_USING_ICU_NAMESPACE=0',
                               '-Wno-ambiguous-reversed-operator', '-Wno-deprecated-declarations']
        if group == 'common': flags += ['-DU_COMMON_IMPLEMENTATION', '-D_REENTRANT']
        if group == 'i18n': flags += ['-DU_I18N_IMPLEMENTATION']
        if group == 'shim': flags += ['-DU_SHOW_CPLUSPLUS_API=0']
        for name in selected: units.append(('icu-' + group + '-' + Path(name).name, icu / name, flags, cxx, group))
    if len(units) != (480 if args.all else 29) or len({x[0] for x in units}) != len(units):
        raise RuntimeError('Native dependency source selection changed')
    runtime_dir = (args.runtime_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/runtime-build/linux') if args.link else None
    record = {'profile': args.profile, 'runtime_executed': False, 'catalog_sha256': digest(CATALOG),
              'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'sources': catalog, 'compiler_version': run([cxx, '--version'], output / 'compiler-version.log')}
    record.update(source_bundle(output, sources, catalog, toolchain, runtime_dir))
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
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compile_one, units))
    record['results'] = results
    result_path = output / ('all-results.json' if args.all else 'preflight-results.json')
    save(result_path, record)
    print('Passed', sum(x['exit'] == 0 for x in results), '/', len(results), flush=True)
    if any(x['exit'] for x in results): return 1
    if args.link:
        def link(name, groups, dependencies):
            inputs = []
            for entry in results:
                if entry['group'] not in groups: continue
                obj = objects / (entry['unit'] + '.o')
                if digest(obj) != entry['object_sha256']: raise RuntimeError('Native object changed before link')
                inputs.append(obj)
            response = output / (name + '.rsp')
            response.write_text('\n'.join('"' + p.as_posix() + '"' for p in inputs) + '\n', encoding='utf-8')
            run([cxx, '-shared', '-Wl,-z,defs', '-Wl,-soname,' + name, '@' + str(response),
                 *[output / p for p in dependencies], '-Wl,-rpath,$ORIGIN', '-pthread', '-ldl', '-lm',
                 '-o', output / name], output / (name + '-link.log'))
        link('libnativehelper.so', ['helper'], ['libart.so'])
        link('libicuuc.so', ['common', 'init', 'stubdata'], [])
        link('libicui18n.so', ['i18n'], ['libicuuc.so'])
        link('libicu.so', ['shim'], ['libicui18n.so', 'libicuuc.so'])
        link('libicu_jni.so', ['jni'], ['libicui18n.so', 'libicuuc.so', 'libnativehelper.so', 'libart.so'])
        data_dir = output / 'i18n/etc/icu'
        data_dir.mkdir(parents=True, exist_ok=True)
        data = data_dir / 'icudt75l.dat'
        shutil.copyfile(icu / 'icu4c/source/stubdata/icudt75l.dat', data)
        harness = output / 'native-icu-check'
        run([cxx, *common, *cpp, ROOT / 'fixtures/art-runtime/native_icu.cpp',
             output / 'libicui18n.so', output / 'libicuuc.so', '-ldl', '-Wl,-rpath,$ORIGIN', '-o', harness],
            output / 'native-icu-check-build.log')
        env = {**os.environ, 'ANDROID_I18N_ROOT': str((output / 'i18n').resolve())}
        start = time.monotonic()
        stdout = run([harness, (output / 'libicu_jni.so').resolve()], output / 'native-icu-check.log', env)
        record['native_check'] = {'stdout': stdout, 'seconds': time.monotonic() - start,
                                  'runtime_executed': False, 'icu_data_sha256': digest(data)}
        record['linked_artifacts'] = {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)}
                                      for p in [*sorted(output.glob('*.so')), harness]}
        save(result_path, record)
        print(stdout, flush=True)
    return 0


if __name__ == '__main__': raise SystemExit(main())
