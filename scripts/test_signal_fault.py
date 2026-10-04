"""Compare translated Android synchronous faults with the same source on Linux."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import zipfile
from environment import ROOT, environment


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--evidence-root', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('This reference requires native ARM64 Linux')
    mode = 'oracle' if args.evidence_root else 'reference'
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / ('m3/signal-fault-' + mode)
    output.mkdir(parents=True, exist_ok=True)
    caller, runner = (ROOT / 'fixtures/kernel-signals' / name for name in ('faults.c', 'faults_linux.c'))
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    sources = (caller, runner, Path(__file__), ROOT / 'LICENSE')
    record = {'project_commit': revision, 'scope': 'Same source with native Linux libc; not the identical Bionic object',
              'kernel': platform.release(), 'machine': platform.machine(),
              'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}}
    with zipfile.ZipFile(output / 'corresponding-source.zip', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sources: archive.write(path, 'artbox/' + path.relative_to(ROOT).as_posix())
    record['source_bundle_sha256'] = digest(output / 'corresponding-source.zip')
    if args.evidence_root:
        evidence = args.evidence_root.resolve()
        producer = evidence / 'artifacts/m2-bionic-startup.json'
        bionic = json.loads(producer.read_text(encoding='utf-8'))
        metadata = bionic['signal_fault']
        obj = evidence / 'build/m2/bionic-startup/signal-fault.o'
        if (bionic['project_commit'] != revision or metadata['fault_cases'] != 5 or
                digest(caller) != metadata['source_sha256'] or digest(obj) != metadata['object_sha256'] or
                any(bionic[m]['signal_fault_cases'] != 5 or bionic[m]['signal_fault_edit_mutation'] != -1006 or
                    bionic[m]['signal_fault_address_mutation'] != -1007 for m in ('native', 'sampled_native'))):
            raise RuntimeError('Fault source, object or signed Bionic results differ from this revision')
        record.update(metadata=metadata, producer_sha256=digest(producer))
    binary = output / 'signal-fault'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18',
               str(runner), str(caller), '-o', str(binary)]
    record['compile_command'] = command
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    record['binary_sha256'] = digest(binary)
    record['runs'] = {}
    for key, option, expected in [('native', None, 5), ('dropped-edit', '--drop-register-edit', -1006),
                                  ('dropped-address', '--drop-fault-address', -1007)]:
        result = subprocess.run([str(binary)] + ([option] if option else []),
                                capture_output=True, text=True, timeout=20)
        record['runs'][key] = {'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
        (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        if key == 'native':
            result.check_returncode()
            if result.stderr or json.loads(result.stdout) != {
                    'fault_cases': 5, 'native_resume': True, 'alternate_stack': True, 'mask_preserved': True}:
                raise RuntimeError('Incomplete Linux synchronous fault contract')
        elif result.returncode != 1 or result.stdout or result.stderr != f'signal fault contract: {expected}\n':
            raise RuntimeError('Fault mutation did not fail the intended assertion')
    print('Native Linux synchronous faults: five cases and two mutation controls pass (' + mode + ')')


if __name__ == '__main__': main()
