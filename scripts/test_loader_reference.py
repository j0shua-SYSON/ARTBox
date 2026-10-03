"""Establish native Linux semantics for the Android loader API fixture."""
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

EXPECTED = {'cases': 32, 'thread_error_checks': 6, 'cleanup': True}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('This reference requires native ARM64 Linux')
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/loader-reference'
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'fixtures/loader-api'
    commands = []

    def run(words, name):
        command = list(map(str, words))
        process = subprocess.run(command, capture_output=True, timeout=60)
        (output / (name + '.log')).write_bytes(process.stdout + process.stderr)
        commands.append(command)
        if process.returncode:
            raise RuntimeError('Loader reference failed; see ' + str(output / (name + '.log')))
        return process.stdout

    common = [args.compiler, '-std=c11', '-O2', '-fPIC', '-fno-builtin', '-fno-stack-protector',
              '-Wall', '-Wextra', '-Werror']
    for name in ['check', 'provider']:
        run([*common, '-c', source / (name + '.c'), '-o', output / (name + '.o')], name + '-build')
    provider = output / 'libartbox_loader_provider.so'
    client = output / 'libartbox_loader_client.so'
    run([args.compiler, '-shared', '-Wl,-z,defs', '-Wl,-soname,' + provider.name,
         output / 'provider.o', '-o', provider], 'provider-link')
    run([args.compiler, '-shared', '-Wl,-z,defs', '-Wl,-soname,' + client.name,
         '-Wl,-rpath,$ORIGIN', output / 'check.o', '-Wl,--no-as-needed', provider,
         '-ldl', '-o', client], 'client-link')
    runner = output / 'check'
    run([*common, source / 'linux.c', '-pthread', '-ldl', '-o', runner], 'runner-build')
    observed = json.loads(run([runner, client], 'reference'))
    if observed != EXPECTED:
        raise RuntimeError('Native loader reference did not complete its contract')
    report = {
        'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'scope': 'Native Linux loader API reference; guest dlfcn implementation not tested',
        'result': observed, 'commands': commands,
        'sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in
                    [source / 'check.c', source / 'provider.c', source / 'linux.c', Path(__file__)]},
        'artifacts': {p.name: digest(p) for p in
                      [provider, client, runner, output / 'check.o', output / 'provider.o']}}
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / 'm3-loader-reference.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Native Linux loader reference: 32 cases, six thread-error checks and cleanup pass')


if __name__ == '__main__':
    main()
