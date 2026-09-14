"""Establish the original Linux proc baseline before virtual proc integration."""
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


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root', type=Path, required=True)
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('aarch64', 'arm64'):
        parser.error('Native Linux ARM64 required')
    evidence = args.evidence_root.resolve()
    r = json.loads((evidence / 'artifacts/m2-bionic-startup.json').read_text(encoding='utf-8'))
    metadata = r['proc']
    obj = evidence / 'build/m2/bionic-startup/proc-check.o'
    if digest(obj) != metadata['object_sha256'] or digest(ROOT / 'fixtures/bionic-files/proc.c') != metadata['source_sha256']:
        raise RuntimeError('Proc caller provenance mismatch')
    build = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm2/proc'
    build.mkdir(parents=True, exist_ok=True)
    records = {}
    for profile in ('upstream', 'native'):
        bionic = json.loads((evidence / f'artifacts/m2-bionic-{profile}.json').read_text(encoding='utf-8'))
        stubs = evidence / f'build/m2/bionic/{profile}/syscall-test.o'
        if digest(stubs) != bionic['syscall_stubs']['test_object_sha256']: raise RuntimeError('Proc syscall provenance mismatch')
        executable = build / profile
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18',
                        str(ROOT / 'fixtures/bionic-files/linux-proc.c'), str(obj), str(stubs), '-o', str(executable)], check=True)
        process = subprocess.run([str(executable)], capture_output=True, text=True, encoding='utf-8', timeout=20)
        (build / (profile + '.log')).write_text(process.stdout + process.stderr, encoding='utf-8')
        process.check_returncode()
        records[profile] = json.loads(process.stdout)
        if records[profile] != {'proc_cases': 22}: raise RuntimeError('Proc baseline mismatch')
    (Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / 'm2-proc-linux.json').write_text(json.dumps({
        'scope': 'Native Linux baseline; virtual proc acceptance is still pending',
        'proc': metadata, 'profiles': records}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__': main()
