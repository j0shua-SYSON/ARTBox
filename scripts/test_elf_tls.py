"""Compare original NDK ELF TLS on native Linux with signed Bionic execution."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
from environment import ROOT, environment
from tls_adapt import adapt


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('Native Linux ARM64 required')
    evidence = args.evidence_root.resolve()
    report = json.loads((evidence / 'artifacts/m2-bionic-startup.json').read_text(encoding='utf-8'))
    inputs = evidence / 'build/m2/bionic-startup'
    metadata = report['tls']
    for name, key in [('tls-original.s', 'original_assembly_sha256'), ('tls-adapted.s', 'adapted_assembly_sha256'),
                      ('tls-original.o', 'original_object_sha256'), ('tls-access.o', 'adapted_object_sha256'),
                      ('tls-abi.o', 'abi_object_sha256'),
                      ('libtls_client_linux.so', 'original_client_sha256')]:
        if digest(inputs / name) != metadata[key]: raise RuntimeError('TLS input hash mismatch: ' + name)
    for name in ('storage', 'access', 'resolver', 'abi'):
        source = ROOT / 'fixtures/tls' / (name + ('.S' if name in ('resolver', 'abi') else '.c'))
        if digest(source) != metadata[name + '_source_sha256']: raise RuntimeError('TLS source mismatch')
    provider, client = inputs / 'libartbox_tls.so', inputs / 'libtls_client_linux.so'
    if digest(provider) != report['images']['tls']['elf_sha256']: raise RuntimeError('TLS provider mismatch')
    changed, changes = adapt((inputs / 'tls-original.s').read_text(encoding='utf-8'))
    if changed != (inputs / 'tls-adapted.s').read_text(encoding='utf-8') or any(metadata[k] != v for k, v in changes.items()):
        raise RuntimeError('TLS adaptation differs from the recorded TP replacements')
    build = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm2/elf-tls'
    build.mkdir(parents=True, exist_ok=True)
    executable = build / 'tls-oracle'
    subprocess.run([args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                    str(ROOT / 'fixtures/tls/linux.c'), str(client), str(provider),
                    '-Wl,-rpath,' + str(inputs), '-o', str(executable)], check=True)
    env = dict(os.environ, LD_LIBRARY_PATH=str(inputs))
    process = subprocess.run([str(executable)], env=env, capture_output=True, text=True, encoding='utf-8', timeout=20)
    (build / 'native.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    process.check_returncode()
    result = json.loads(process.stdout)
    expected = {'tls_threads': 7, 'tls_modules': 2, 'tls_result': 0}
    if result != expected or any(report[mode][key] != value for mode in ('native', 'sampled_native') for key, value in expected.items()):
        raise RuntimeError('Native Linux and signed Bionic TLS results disagree')
    (Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / 'm2-elf-tls-linux.json').write_text(json.dumps({
        'scope': 'Two original ELF TLS templates, six concurrent workers plus main, compared with signed Bionic',
        'provider_sha256': digest(provider), 'tls': metadata, **result}, indent=2) + '\n', encoding='utf-8')
    print('Original Linux ELF and both signed Bionic profiles pass two-module TLS across seven threads')


if __name__ == '__main__': main()
