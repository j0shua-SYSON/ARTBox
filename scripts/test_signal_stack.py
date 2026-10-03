"""Validate alternate-stack wire behavior and the same-source native handler on Linux."""
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
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / ('m3/signal-stack-' + mode)
    output.mkdir(parents=True, exist_ok=True)
    wire, handler, runner = (ROOT / 'fixtures/kernel-signals' / name for name in ('stack.c', 'handler_stack.c', 'stack_linux.c'))
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    record = {'project_commit': revision, 'scope': 'Native Linux same-source handler, identical wire object when supplied',
              'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in (wire, handler, runner, Path(__file__))}}
    wire_input = wire
    if args.evidence_root:
        evidence = args.evidence_root.resolve()
        producer = evidence / 'artifacts/m2-bionic-startup.json'
        bionic = json.loads(producer.read_text(encoding='utf-8'))
        metadata = bionic['signal_stack']
        wire_input = evidence / 'build/m2/bionic-startup/signal-stack.o'
        handler_object = evidence / 'build/m2/bionic-startup/signal-stack-handler.o'
        if (bionic['project_commit'] != revision or metadata['wire_cases'] != 17 or metadata['handler_cases'] != 24 or
                digest(wire) != metadata['wire_source_sha256'] or digest(handler) != metadata['handler_source_sha256'] or
                digest(wire_input) != metadata['wire_object_sha256'] or digest(handler_object) != metadata['handler_object_sha256'] or
                any(bionic[m]['signal_stack_cases'] != 17 or bionic[m]['signal_stack_handler_cases'] != 24 or
                    bionic[m]['signal_stack_mutation'] != -1001 or bionic[m]['signal_stack_threads'] != 1
                    for m in ('native', 'sampled_native'))):
            raise RuntimeError('Alternate-stack caller or signed results differ from this revision')
        record.update(metadata=metadata, producer_sha256=digest(producer))
    binary = output / 'signal-stack'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18', '-pthread',
               str(runner), str(wire_input), str(handler), '-o', str(binary)]
    record['compile_command'] = command
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    record['binary_sha256'] = digest(binary)
    record['runs'] = {}
    for key, options in (('native', []), ('dropped-onstack', ['--drop-onstack'])):
        executed = subprocess.run([str(binary), *options], capture_output=True, text=True, timeout=15)
        record['runs'][key] = {'exit': executed.returncode, 'stdout': executed.stdout, 'stderr': executed.stderr}
        (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        if key == 'native':
            executed.check_returncode()
            actual = json.loads(executed.stdout)
            if ({k: actual[k] for k in ('wire_cases', 'handler_cases', 'workers')} !=
                    {'wire_cases': 17, 'handler_cases': 24, 'workers': 1} or actual['kernel_minimum_bytes'] <= 0 or executed.stderr):
                raise RuntimeError('Native alternate-stack contract did not complete')
        elif executed.returncode != 1 or executed.stdout or executed.stderr != 'signal stack contract: wire=17 handler=-1001\n':
            raise RuntimeError('Omitted SA_ONSTACK did not fail the intended assertion')
    print('Native Linux alternate stack: 17 wire checks, 24 handler checks and missing-flag mutation pass (' + mode + ')')


if __name__ == '__main__':
    main()
