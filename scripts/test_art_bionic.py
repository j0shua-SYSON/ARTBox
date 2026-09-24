"""Compare ART's added Bionic libc checks with native Linux libc."""
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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--compiler', default='cc')
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('This reference requires native ARM64 Linux')
    source = ROOT / 'fixtures/art-bionic/check.c'
    producer = args.evidence_root / 'artifacts/m2-bionic-startup.json'
    report = json.loads(producer.read_text(encoding='utf-8'))
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if report['project_commit'] != head or report['art_libc']['source_sha256'] != digest(source):
        raise RuntimeError('ART libc producer differs from the checked-out source')
    if report['art_libc']['cases'] != 30 or any(report[m]['art_libc_cases'] != 30 for m in ['native', 'sampled_native']):
        raise RuntimeError('Both signed Bionic modes must pass the ART libc checks')
    obj = args.evidence_root / 'build/m2/bionic-startup/art-libc-check.o'
    if digest(obj) != report['art_libc']['object_sha256']:
        raise RuntimeError('Signed libc client object changed')
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/art-bionic'
    output.mkdir(parents=True, exist_ok=True)
    binary = output / 'check'
    command = [args.compiler, '-std=c11', '-O2', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
               str(source), str(ROOT / 'fixtures/art-bionic/linux.c'), '-o', str(binary)]
    result = subprocess.run(command, capture_output=True)
    (output / 'build.log').write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    result = subprocess.run([str(binary)], capture_output=True, timeout=30)
    (output / 'reference.log').write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    observed = json.loads(result.stdout)
    if observed != {'cases': 30, 'result': 30}:
        raise RuntimeError('Native Linux libc checks differ from signed Bionic')
    record = {'project_commit': head, 'native_linux': observed, 'signed_bionic_modes': ['native', 'sampled_native'],
              'producer_sha256': digest(producer), 'source_sha256': digest(source),
              'reference_binary_sha256': digest(binary), 'command': command}
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / 'm3-art-bionic-linux.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('ART libc dependency checks: 30 native Linux cases match both signed Bionic modes')


if __name__ == '__main__':
    main()
