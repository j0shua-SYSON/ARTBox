"""Measure native Linux ARM64 signal behavior before implementing the guest boundary."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

from environment import ROOT, environment


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('This reference requires native ARM64 Linux')
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/signal-reference'
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'fixtures/kernel-signals/linux.c'
    binary = output / 'signal-reference'
    command = [args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffixed-x18',
               '-pthread', str(source), '-o', str(binary)]
    record = {'scope': 'Native Linux reference only; no guest signal translation or ART JavaVM',
              'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in (source, Path(__file__))},
              'kernel': platform.release(), 'machine': platform.machine(), 'compile_command': command,
              'compiler': subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
              'passed': False, 'runs': {}}
    result = output / 'result.json'

    def save():
        result.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')

    save()
    compiled = subprocess.run(command, capture_output=True, timeout=60)
    (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        raise RuntimeError('Signal reference compilation failed; see ' + str(output / 'compile.log'))
    record['binary_sha256'] = digest(binary)
    for mode in ('reference', 'dropped-worker-signal'):
        invocation = [str(binary)] + (['--drop-worker-signal'] if mode != 'reference' else [])
        started = time.monotonic()
        executed = subprocess.run(invocation, capture_output=True, text=True, timeout=20)
        (output / (mode + '.stdout')).write_text(executed.stdout, encoding='utf-8')
        (output / (mode + '.stderr')).write_text(executed.stderr, encoding='utf-8')
        record['runs'][mode] = {'exit': executed.returncode, 'seconds': time.monotonic() - started,
                                'stdout': executed.stdout, 'stderr': executed.stderr}
        save()
        if mode == 'reference':
            if executed.returncode or executed.stderr:
                raise RuntimeError('Signal reference failed; see ' + str(output / (mode + '.stderr')))
            observed = json.loads(executed.stdout)
            if (not observed['checks'] or not 0 < observed['kernel_min_altstack'] <= 128 * 1024 or
                    any(observed[key] is not True for key in
                        ('handler_delivery', 'alternate_stack', 'standard_coalescing', 'thread_wakeup'))):
                raise RuntimeError('Signal reference did not complete its contract')
            record['reference'] = observed
        elif (executed.returncode != 1 or executed.stdout or
              'worker_result == SIGUSR1 && worker_info.si_pid == pid && worker_info.si_code == SI_TKILL' not in executed.stderr):
            raise RuntimeError('Missing worker delivery was not detected by the signal reference')
    record['passed'] = True
    save()
    print('Native Linux signal registration, alternate-stack delivery, coalescing and thread wakeup pass; '
          'missing worker delivery is rejected')


if __name__ == '__main__':
    main()
