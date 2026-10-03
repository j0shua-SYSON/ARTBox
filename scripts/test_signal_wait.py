"""Run the shared signal-wait contract against the native ARM64 Linux kernel."""
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
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / ('m3/signal-wait-' + mode)
    output.mkdir(parents=True, exist_ok=True)
    caller = ROOT / 'fixtures/kernel-signals/wait.c'
    runner = ROOT / 'fixtures/kernel-signals/wait_linux.c'
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    record = {'project_commit': revision, 'scope': 'Native Linux blocked signal wait contract',
              'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in (caller, runner, Path(__file__))}}
    source = caller
    if args.evidence_root:
        evidence = args.evidence_root.resolve()
        producer = evidence / 'artifacts/m2-bionic-startup.json'
        bionic = json.loads(producer.read_text(encoding='utf-8'))
        source = evidence / 'build/m2/bionic-startup/signal-wait.o'
        metadata = bionic['signal_wait']
        if (bionic['project_commit'] != revision or metadata['cases'] != 33 or
                digest(caller) != metadata['source_sha256'] or digest(source) != metadata['object_sha256'] or
                any(bionic[mode]['signal_wait_cases'] != 33 for mode in ('native', 'sampled_native'))):
            raise RuntimeError('Signal wait caller differs from signed Bionic acceptance')
        record.update(metadata=metadata, producer_sha256=digest(producer))
    binary = output / 'signal-wait'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18',
               str(runner), str(source), '-o', str(binary)]
    record['compile_command'] = command
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    executed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    (output / 'native.stdout').write_text(executed.stdout, encoding='utf-8')
    (output / 'native.stderr').write_text(executed.stderr, encoding='utf-8')
    record.update(exit=executed.returncode, binary_sha256=digest(binary))
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    executed.check_returncode()
    record['native'] = json.loads(executed.stdout)
    if record['native'] != {'cases': 33, 'result': 33} or executed.stderr:
        raise RuntimeError('Native signal wait contract did not complete')
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Native Linux signal wait: 33 cases pass (' + mode + ')')


if __name__ == '__main__':
    main()
