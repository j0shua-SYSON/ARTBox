"""Compare signed Bionic handler behavior with the same source on native Linux."""
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
    parser.add_argument('--compiler', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--evidence-root', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('This reference requires native ARM64 Linux')
    mode = 'oracle' if args.evidence_root else 'reference'
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / ('m3/signal-handler-' + mode)
    output.mkdir(parents=True, exist_ok=True)
    caller = ROOT / 'fixtures/kernel-signals/handler.c'
    runner = ROOT / 'fixtures/kernel-signals/handler_linux.c'
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    record = {'project_commit': revision, 'scope': 'Same C source with native Linux libc; not an identical Bionic object',
              'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in (caller, runner, Path(__file__))}}
    if args.evidence_root:
        evidence = args.evidence_root.resolve()
        producer = evidence / 'artifacts/m2-bionic-startup.json'
        bionic = json.loads(producer.read_text(encoding='utf-8'))
        obj = evidence / 'build/m2/bionic-startup/signal-handler.o'
        metadata = bionic['signal_handler']
        if (bionic['project_commit'] != revision or metadata['cases'] != 16 or
                digest(caller) != metadata['source_sha256'] or digest(obj) != metadata['object_sha256'] or
                any(bionic[m]['signal_handler_cases'] != 16 or bionic[m]['signal_handler_mutation'] != -1000
                    for m in ('native', 'sampled_native'))):
            raise RuntimeError('Handler source or signed Bionic results differ from this revision')
        record.update(metadata=metadata, producer_sha256=digest(producer))
    binary = output / 'signal-handler'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18',
               str(runner), str(caller), '-o', str(binary)]
    record['compile_command'] = command
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    record['binary_sha256'] = digest(binary)
    record['runs'] = {}
    for key, options in (('native', []), ('dropped-edit', ['--drop-register-edit'])):
        executed = subprocess.run([str(binary), *options], capture_output=True, text=True, timeout=10)
        record['runs'][key] = {'exit': executed.returncode, 'stdout': executed.stdout, 'stderr': executed.stderr}
        (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        if key == 'native':
            executed.check_returncode()
            if json.loads(executed.stdout) != {'cases': 16, 'deliveries': 1, 'registers_resumed': True} or executed.stderr:
                raise RuntimeError('Native handler contract did not complete')
        elif executed.returncode != 1 or executed.stdout or executed.stderr != 'signal handler contract: -1000\n':
            raise RuntimeError('Dropped register edits did not fail the intended assertion')
    print('Native Linux signal handler: 16 checks and register mutation pass (' + mode + ')')


if __name__ == '__main__':
    main()
