"""Run the same timeout acceptance source with native Linux pthread types."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from environment import ROOT, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root', type=Path, required=True)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux': parser.error('Native Linux pthreads required')
    report = json.loads((args.evidence_root / 'artifacts/m2-bionic-startup.json').read_text(encoding='utf-8'))
    source = ROOT / 'fixtures/bionic-startup/timeouts.c'
    if hashlib.sha256(source.read_bytes()).hexdigest() != report['timeouts']['source_sha256']:
        raise RuntimeError('Timeout source differs from the signed Bionic input')
    build = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm2/timeouts'
    build.mkdir(parents=True, exist_ok=True)
    executable = build / 'timeout-oracle'
    subprocess.run([args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                    str(source), str(ROOT / 'fixtures/bionic-startup/linux-timeouts.c'), '-o', str(executable)], check=True)
    process = subprocess.run([str(executable)], capture_output=True, text=True, encoding='utf-8', timeout=20)
    (build / 'native.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    process.check_returncode()
    result = json.loads(process.stdout)
    if result != {'timeout_cases': 18} or any(report[mode]['timeout_cases'] != 18 for mode in ('native', 'sampled_native')):
        raise RuntimeError('Pthread timeout expectations disagree')
    (Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / 'm2-timeouts-linux.json').write_text(json.dumps({
        'scope': 'Same C source compiled against each runtime pthread ABI; no shared opaque pthread structures',
        'timeouts': report['timeouts'], **result}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__': main()
