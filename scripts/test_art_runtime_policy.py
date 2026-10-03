"""Test the forbidden JIT factory and stack-trace formatting; does not start ART."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import subprocess
import tempfile
import time

from environment import ROOT, environment
from sources import obtain
from art_runtime_policy import without_rust_demangler
from test_low_address import macho


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    build = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/runtime-policy'
    build.mkdir(parents=True, exist_ok=True)
    attempt = Path(tempfile.mkdtemp(prefix='attempt-', dir=build))
    names = ('art-runtime-policy', 'unwindstack-demangle', 'rust-demangle-test-header',
             'libbase-dex', 'fmtlib-references')
    sources = {name: obtain(name) for name in names}
    art, unwind, rust, base, fmt = (sources[name] for name in names)
    specs = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))
    original = unwind / 'libunwindstack/Demangle.cpp'
    adapted = attempt / 'Demangle.cpp'
    adapted.write_bytes(without_rust_demangler(original))
    factory = ROOT / 'third_party/art/adapters/no_jit.cpp'
    check = ROOT / 'fixtures/art-runtime-policy/check.cpp'
    record = {
        'scope': 'Native policy/formatting functions only; no ART boot or Java execution',
        'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], encoding='utf-8').strip(),
        'platform': sys.platform, 'architecture': platform.machine(),
        'sources': {name: specs[name] for name in names},
        'project_sources': {name: digest(ROOT / name) for name in (
            'scripts/test_art_runtime_policy.py', 'scripts/art_runtime_policy.py',
            'third_party/art/adapters/no_jit.cpp', 'fixtures/art-runtime-policy/check.cpp')},
        'upstream_sha256': digest(original), 'adapted_sha256': digest(adapted),
        'art_runtime_executed': False, 'device_execution_verified': False,
        'runtime_code_generation_checked': False,
    }
    notice_dir = attempt / 'notices'
    notice_dir.mkdir()
    for name, source in sources.items():
        (notice_dir / (name + '.txt')).write_bytes((source / specs[name]['notice']).read_bytes())
    (notice_dir / 'rust-test-header-APACHE.txt').write_bytes((rust / 'LICENSE-APACHE').read_bytes())
    (notice_dir / 'fmt-NOTICE.txt').write_bytes((fmt / 'NOTICE').read_bytes())
    # The selected unwindstack source/header carry Apache notices; retain the
    # complete ART Apache text alongside unwindstack's own BSD license.
    record['notices'] = {path.name: digest(path) for path in notice_dir.iterdir()}
    includes = [art / 'compiler/export', art / 'libartbase', base / 'include', fmt / 'include',
                unwind / 'libunwindstack/include', rust / 'include']
    flags = ['-std=c++20', '-O2', '-DNDEBUG', '-DFMT_HEADER_ONLY', '-fno-exceptions']
    # The pinned fmt Windows stream header uses dynamic_cast even when unused.
    # Native Windows tests retain RTTI; Apple/Android runtime builds do not need it.
    if sys.platform != 'win32':
        flags += ['-fno-rtti']
    for path in includes:
        flags += ['-I', str(path)]
    if sys.platform == 'darwin':
        if platform.machine().lower() not in ('arm64', 'aarch64'):
            raise RuntimeError('Signed policy checks require a native ARM64 Mac')
        compiler = ['xcrun', '--sdk', 'macosx', 'clang++']
        target = ['-target', 'arm64-apple-macos11.0', '-isysroot',
                  subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], encoding='utf-8').strip()]
    else:
        compiler, target = [args.cxx], []
    record['compiler'] = subprocess.check_output([*compiler, '--version'], encoding='utf-8').splitlines()[0]

    def compile_target(label, command):
        process = subprocess.run([str(x) for x in command], capture_output=True, encoding='utf-8')
        (attempt / (label + '-build.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        if process.returncode:
            raise RuntimeError('Policy compilation failed; see ' + str(attempt / (label + '-build.log')))

    record['executables'] = {}
    for kind, source, define in (('original', original, '-DARTBOX_UPSTREAM_CONTROL'),
                                 ('adapted', adapted, '-DARTBOX_NO_RUST_DEMANGLE')):
        binary = attempt / (kind + ('.exe' if os.name == 'nt' else ''))
        compile_target(kind, [*compiler, *target, *flags, define, '-DARTBOX_POLICY_EXECUTABLE',
                              source, factory, check, '-o', binary])
        if sys.platform == 'darwin':
            subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(binary)], check=True)
            record['executables'][kind] = macho(binary, 1, 2**32)
        else:
            record['executables'][kind] = {'sha256': digest(binary), 'bytes': binary.stat().st_size}

    record['cases'] = {}
    expected_cases = (
        ('original-names', 'original', [], 3, '', 'Optional Rust demangler was invoked 2 times\n'),
        ('adapted-names', 'adapted', [], 0, '8 stack trace name cases passed; no Rust runtime invoked\n', ''),
        ('forbidden-jit', 'adapted', ['jit'], 126, '', 'ARTBox: runtime JIT compiler creation is forbidden\n'),
    )
    for name, kind, arguments, code, stdout, stderr in expected_cases:
        binary = attempt / (kind + ('.exe' if os.name == 'nt' else ''))
        started = time.monotonic()
        process = subprocess.run([str(binary), *arguments], capture_output=True, encoding='utf-8', timeout=30)
        result = {'exit': process.returncode, 'stdout': process.stdout, 'stderr': process.stderr,
                  'elapsed_ms': (time.monotonic() - started) * 1000}
        record['cases'][name] = result
        (attempt / (name + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        if (process.returncode, process.stdout, process.stderr) != (code, stdout, stderr):
            raise RuntimeError('Unexpected policy test outcome: ' + name)

    if sys.platform == 'darwin':
        framework = attempt / 'ARTBoxRuntimePolicy.framework'
        framework.mkdir()
        binary = framework / 'ARTBoxRuntimePolicy'
        sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], encoding='utf-8').strip()
        compile_target('ios', ['xcrun', '--sdk', 'iphoneos', 'clang++', '-target', 'arm64-apple-ios15.0',
                              '-isysroot', sdk, *flags, '-DARTBOX_NO_RUST_DEMANGLE', '-dynamiclib',
                              adapted, factory, check, '-Wl,-dead_strip',
                              '-Wl,-exported_symbol,_artbox_demangle_check',
                              '-Wl,-exported_symbol,_artbox_jit_forbidden_probe',
                              '-Wl,-install_name,@rpath/ARTBoxRuntimePolicy.framework/ARTBoxRuntimePolicy',
                              '-o', binary])
        info = {'CFBundleIdentifier': 'org.artbox.RuntimePolicy', 'CFBundleExecutable': 'ARTBoxRuntimePolicy',
                'CFBundleName': 'ARTBoxRuntimePolicy', 'CFBundlePackageType': 'FMWK', 'CFBundleVersion': '1',
                'CFBundleShortVersionString': '1.0', 'MinimumOSVersion': '15.0',
                'CFBundleSupportedPlatforms': ['iPhoneOS']}
        (framework / 'Info.plist').write_bytes(plistlib.dumps(info))
        for notice in notice_dir.iterdir():
            (framework / notice.name).write_bytes(notice.read_bytes())
        subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(framework)], check=True)
        record['ios_framework'] = macho(binary, 2, None, kind=6)
    record['native_functions_executed'] = True
    (attempt / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    (build / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Runtime policy: eight names and forbidden factory pass; ART execution remains false')


if __name__ == '__main__':
    main()
