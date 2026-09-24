"""Build ART's AOSP math dependency and execute its dynamic client on Mac/Linux."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import zipfile

from environment import ROOT, environment
from sources import obtain
from ndk import obtain as obtain_ndk
from bionic_adapt import inventory
from dynamic_bundle import prepare

EXPECTED = {'cases': 78, 'first_failure': 0, 'cleanup': True}
IMPORTS = sorted('acos asin atan atan2 cbrt cos cosh exp expm1 fmod fmodf hypot log log10 nextafter pow sin sinh tan tanh'.split())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def run(command, log=None):
    result = subprocess.run(list(map(str, command)), capture_output=True)
    if log:
        log.write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('Command failed' + (': ' + str(log) if log else ': ' + result.stderr.decode('utf-8', 'replace')))
    return result.stdout


def execute(runner, arguments, log):
    result = subprocess.run([str(runner), *map(str, arguments)], capture_output=True, timeout=30)
    log.write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    observed = json.loads(result.stdout)
    if observed != EXPECTED:
        raise RuntimeError('Math checks returned an unexpected result')
    return observed


def build(args, output, artifacts):
    pins = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    selection = json.loads((ROOT / 'third_party/bionic/art-math.json').read_text(encoding='utf-8'))
    if selection['bionic_commit'] != pins['bionic']['commit'] or selection['arm_math_commit'] != pins['arm-math']['commit']:
        raise RuntimeError('Math selection differs from the pinned AOSP revisions')
    bionic, arm = obtain('bionic'), obtain('arm-math')
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    tc = ndk / 'toolchains/llvm/prebuilt' / host
    suffix = '.exe' if os.name == 'nt' else ''
    tool = lambda name: tc / 'bin' / (name + suffix)
    common = ['--target=aarch64-linux-android35', '-O2', '-fPIC', '-fno-builtin', '-fno-math-errno',
              '-march=armv8-a', '-mno-outline-atomics', '-mbranch-protection=none',
              '-ffixed-x18', '-ffixed-x27', '-ffixed-x28', '-ffunction-sections', '-fdata-sections',
              '-DFLT_EVAL_METHOD=0', '-Wall', '-Werror']
    # These are the pinned AOSP libm and arm-optimized-routines build settings.
    bsd = ['-D_BSD_SOURCE', '-include', 'freebsd-compat.h', '-include', 'fenv-access.h',
           '-Wno-missing-braces', '-Wno-parentheses', '-Wno-sign-compare', '-Wno-static-in-inline',
           '-Wno-unknown-pragmas', '-Wno-unused-const-variable', '-Wno-unused-variable']
    for name in ['libm', 'libm/upstream-freebsd/android/include', 'libm/upstream-freebsd/lib/msun/src',
                 'libm/upstream-freebsd/lib/msun/ld128', 'libc', 'libc/include']:
        bsd += ['-I', bionic / name]
    arm_flags = ['-DWANT_ERRNO=0', '-DWANT_VMATH=0', '-DHAVE_FAST_FMA=1',
                 '-D__BIONIC_LP32_USE_LONG_DOUBLE', '-ffp-contract=fast', '-Wno-unused-parameter',
                 '-I', arm / 'math/include']
    objects, records = [], []
    for owner, source, names in [('bionic', bionic, selection['bionic_sources']),
                                 ('arm-math', arm, selection['arm_math_sources'])]:
        for name in names:
            path = source / name
            if not path.resolve().is_relative_to(source.resolve()):
                raise RuntimeError('Math input escapes its pinned source tree')
            if owner == 'bionic' and digest(path) != names[name]:
                raise RuntimeError('Bionic math source changed: ' + name)
            obj = output / (owner + '-' + path.name + '.o')
            cxx = path.suffix == '.cpp'
            language = ['-std=gnu++20', '-fno-exceptions', '-fno-rtti', '-nostdinc++'] if cxx else ['-std=gnu99']
            command = [tool('clang++' if cxx else 'clang'), *common, *language,
                       *(bsd if owner == 'bionic' else arm_flags), '-c', path, '-o', obj]
            run(command, output / (obj.name + '.log'))
            objects.append(obj)
            records.append({'owner': owner, 'source': name, 'source_sha256': digest(path),
                            'object': obj.name, 'object_sha256': digest(obj), 'command': list(map(str, command))})
    builtins = tc / 'lib/clang/19/lib/linux/libclang_rt.builtins-aarch64-android.a'
    ndk_notice = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
    notices = {'BIONIC-LIBM-NOTICE.txt': (bionic / 'libm/NOTICE', selection['libm_notice_sha256']),
               'BIONIC-LIBC-NOTICE.txt': (bionic / pins['bionic']['notice'], pins['bionic']['notice_sha256']),
               'ARM-MATH-LICENSE.txt': (arm / pins['arm-math']['notice'], pins['arm-math']['notice_sha256']),
               'NDK-NOTICE.txt': (tc / 'NOTICE', ndk_notice),
               'ARTBOX-LICENSE.txt': (ROOT / 'LICENSE', digest(ROOT / 'LICENSE'))}
    for path, expected in notices.values():
        if digest(path) != expected:
            raise RuntimeError('Math notice differs from its reviewed pin: ' + str(path))
    ldflags = ['-shared', '--hash-style=both', '--build-id=none', '-z', 'defs', '-z', 'max-page-size=16384',
               '--pack-dyn-relocs=relr', '-T', ROOT / 'fixtures/bionic-dynamic/image.ld']
    library, client = output / 'libm.so', output / 'libartbox-math-check.so'
    run([tool('ld.lld'), *ldflags, '-Bsymbolic-functions', '-soname', library.name,
         '--why-extract=' + str(output / 'builtins-extracted.txt'), *objects, builtins, '-o', library], output / 'library-link.log')
    obj = output / 'check.o'
    run([tool('clang'), *common, '-std=c11', '-Wextra', '-c', ROOT / 'fixtures/art-math/check.c', '-o', obj], output / 'client-build.log')
    run([tool('ld.lld'), *ldflags, '-soname', client.name, '-rpath', '$ORIGIN', obj, library, '-o', client], output / 'client-link.log')
    binaries = {}
    for name, elf in [('library', library), ('client', client)]:
        disassembly = run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf]).decode()
        (output / (name + '.disassembly.txt')).write_text(disassembly, encoding='utf-8')
        counts = inventory(disassembly)
        if not counts['instruction_count'] or any(v for k, v in counts.items() if k != 'instruction_count'):
            raise RuntimeError('Math code violates the signed native instruction boundary')
        undefined = run([tool('llvm-nm'), '--dynamic', '--undefined-only', '--format=posix', elf]).decode()
        imports = sorted(line.split()[0] for line in undefined.splitlines())
        if imports != ([] if name == 'library' else IMPORTS):
            raise RuntimeError('Unexpected math imports: ' + repr(imports))
        binaries[name] = {'filename': elf.name, 'sha256': digest(elf), 'bytes': elf.stat().st_size,
                          'inventory': counts, 'imports': imports}
        run([sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', elf, output / (name + '-pack')])
    project = ['scripts/test_art_math.py', 'scripts/environment.py', 'scripts/sources.py', 'scripts/ndk.py',
               'scripts/bionic_adapt.py', 'scripts/dynamic_bundle.py', 'scripts/guest_bundle.py',
               'tools/wrap_dynamic.py', 'tools/pack_elf.py', 'fixtures/bionic-dynamic/image.ld',
               'fixtures/art-math/check.c', 'fixtures/art-math/linux.c', 'tests/native_art_math.c',
               'third_party/sources.json', 'third_party/bionic/art-math.json', 'third_party/bionic/builtins.json',
               'CMakeLists.txt', '.github/workflows/host-tests.yml', 'docs/m3-math.md', 'THIRD_PARTY.md', 'LICENSE']
    # Retain original headers and their per-file terms with the selected sources.
    upstream = {'bionic/' + r['source']: bionic / r['source'] for r in records if r['owner'] == 'bionic'}
    for directory in [bionic / 'libm', bionic / 'libc/include']:
        for p in directory.rglob('*.h'):
            upstream['bionic/' + p.relative_to(bionic).as_posix()] = p
    upstream['bionic/libm/Android.bp'] = bionic / 'libm/Android.bp'
    upstream.update({'arm-math/' + f['path']: arm / f['path'] for f in pins['arm-math']['files']})
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name, path in sorted(upstream.items()): z.write(path, 'upstream/' + name)
        for name, (path, _) in notices.items(): z.write(path, 'notices/' + name)
        for name in project: z.write(ROOT / name, 'artbox/' + name)
    report = {'project_commit': run(['git', 'rev-parse', 'HEAD']).decode().strip(),
              'scope': 'AOSP math subset for ART; not complete libm or ART startup',
              'selection': selection, 'objects': records, 'binaries': binaries,
              'builtins_archive_sha256': digest(builtins), 'client_object_sha256': digest(obj),
              'project_sources': {p: digest(ROOT / p) for p in project},
              'upstream_sources': {n: digest(p) for n, p in upstream.items()},
              'source_bundle_sha256': digest(bundle), 'notices': {n: h for n, (_, h) in notices.items()},
              'frameworks': {}, 'device_execution_verified': False}
    save(artifacts / 'm3-art-math.json', report)
    if sys.platform == 'darwin':
        if platform.machine().lower() not in ('arm64', 'aarch64'):
            raise RuntimeError('Signed math execution requires ARM64 macOS')
        for target in ('macos', 'ios'):
            frameworks = {}
            for name in ('library', 'client'):
                binary, details = prepare(output / (name + '-pack'), output / target / name, target,
                    bionic / 'libm/NOTICE', selection['libm_notice_sha256'],
                    {n: pair for n, pair in notices.items() if n != 'BIONIC-LIBM-NOTICE.txt'},
                    name='ARTBoxMath' if name == 'library' else 'ARTBoxMathCheck', notice_name='BIONIC-LIBM-NOTICE.txt')
                frameworks[name] = binary
                report['frameworks'][target + '-' + name] = details
            if target == 'macos':
                runner = Path(os.environ['ARTBOX_BUILD_DIR']) / 'host/artbox_native_art_math'
                report['native'] = execute(runner, [frameworks['client'], client, frameworks['library'], library], output / 'native.log')
            save(artifacts / 'm3-art-math.json', report)
    print('Source-built math: 35 units and 20 client imports linked; ' +
          ('78 signed Mac cases pass, iOS 15 frameworks verified' if sys.platform == 'darwin' else 'native execution pending'))


def linux(args, output, artifacts):
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        raise RuntimeError('The math oracle requires native ARM64 Linux')
    evidence = args.evidence_root.resolve()
    record = json.loads((evidence / 'artifacts/m3-art-math.json').read_text(encoding='utf-8'))
    if record['project_commit'] != run(['git', 'rev-parse', 'HEAD']).decode().strip():
        raise RuntimeError('Math evidence belongs to a different revision')
    for path, expected in record['project_sources'].items():
        if digest(ROOT / path) != expected: raise RuntimeError('Math project source differs: ' + path)
    source = evidence / 'build/m3/art-math'
    if digest(source / 'corresponding-source.zip') != record['source_bundle_sha256']:
        raise RuntimeError('Math source bundle changed')
    for details in record['binaries'].values():
        if digest(source / details['filename']) != details['sha256']:
            raise RuntimeError('Math ELF changed: ' + details['filename'])
    runner = output / 'math-linux'
    run([args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
         ROOT / 'fixtures/art-math/linux.c', '-ldl', '-o', runner], output / 'runner-build.log')
    observed = execute(runner, [source / 'libartbox-math-check.so'], output / 'guest.log')
    reference = output / 'host-math.so'
    run([args.compiler, '-std=c11', '-O2', '-fPIC', '-shared', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
         ROOT / 'fixtures/art-math/check.c', '-lm', '-o', reference], output / 'reference-build.log')
    native = execute(runner, [reference], output / 'reference.log')
    if observed != record['native'] or native != observed:
        raise RuntimeError('Android math, signed Mac and Linux system libm checks disagree')
    save(artifacts / 'm3-art-math-linux.json', {'project_commit': record['project_commit'],
         'guest': observed, 'system_reference': native,
         'mac_report_sha256': digest(evidence / 'artifacts/m3-art-math.json')})
    print('Native Linux: 78 Android math cases and 78 system-libm reference cases pass')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--evidence-root', type=Path)
    parser.add_argument('--compiler', default='cc')
    args = parser.parse_args()
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/art-math'
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    output.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    (linux if args.evidence_root else build)(args, output, artifacts)


if __name__ == '__main__':
    main()
