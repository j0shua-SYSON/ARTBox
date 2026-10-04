"""Compare queued thread interruptions and EINTR with native ARM64 Linux."""
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
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / ('m3/signal-interrupt-' + mode)
    output.mkdir(parents=True, exist_ok=True)
    queued, caller, runner = [ROOT / 'fixtures/kernel-signals' / name for name in
                              ('realtime.c', 'interrupt.c', 'interrupt_linux.c')]
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    record = {'project_commit': revision, 'scope': 'Same-source native Linux handler and raw queue ABI',
              'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in (queued, caller, runner, Path(__file__))}}
    if args.evidence_root:
        evidence = args.evidence_root.resolve()
        producer = evidence / 'artifacts/m2-bionic-startup.json'
        bionic = json.loads(producer.read_text(encoding='utf-8'))
        metadata = bionic['signal_interrupt']
        if (bionic['project_commit'] != revision or metadata['realtime_cases'] != 26 or
                metadata['handler_groups'] != 4 or metadata['workers'] != 1 or
                digest(queued) != metadata['queue_source_sha256'] or digest(caller) != metadata['handler_source_sha256'] or
                digest(evidence / 'build/m2/bionic-startup/signal-realtime.o') != metadata['queue_object_sha256'] or
                digest(evidence / 'build/m2/bionic-startup/signal-interrupt.o') != metadata['handler_object_sha256'] or
                any(bionic[m]['signal_realtime_cases'] != 26 or bionic[m]['signal_interrupt_cases'] != 4 or
                    bionic[m]['signal_interrupt_mutation'] != -1008 or bionic[m]['signal_interrupt_threads'] != 1
                    for m in ('native', 'sampled_native'))):
            raise RuntimeError('Signed interruption source or result differs from this revision')
        queued = evidence / 'build/m2/bionic-startup/signal-realtime.o'
        record.update(metadata=metadata, producer_sha256=digest(producer))
    binary = output / 'signal-interrupt'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18', '-mno-outline-atomics', '-pthread',
               str(runner), str(caller), str(queued), '-o', str(binary)]
    record['compile_command'] = command
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    if compiled.returncode: print(compiled.stderr.decode(errors='replace'))
    compiled.check_returncode()
    record.update(binary_sha256=digest(binary), runs={})
    for key, options in (('native', []), ('dropped-send', ['--drop-send'])):
        executed = subprocess.run([str(binary), *options], capture_output=True, text=True, timeout=15)
        record['runs'][key] = {'exit': executed.returncode, 'stdout': executed.stdout, 'stderr': executed.stderr}
        (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        if key == 'native':
            if executed.returncode:
                print(executed.stderr, file=sys.stderr, end='')
            executed.check_returncode()
            if json.loads(executed.stdout) != {'realtime_cases': 26, 'interruption_groups': 4, 'workers': 1} or executed.stderr:
                raise RuntimeError('Native queued interruption contract failed')
        elif executed.returncode != 1 or executed.stdout or executed.stderr != 'signal interruption contract: -1008\n':
            raise RuntimeError('Dropped queued send did not fail the intended observation')
    print('Native Linux: 26 realtime cases, queued handlers, interrupted futex and dropped-send control pass (' + mode + ')')


if __name__ == '__main__': main()
