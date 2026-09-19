"""Exercise the native Linux diagnostic that denies runtime executable mappings."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time

from environment import ROOT, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine().lower() not in ('aarch64', 'arm64', 'x86_64', 'amd64'):
        parser.error('Execution requires native ARM64 or x86-64 Linux')
    os.environ.update(environment())
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/runtime-policy/codegen').resolve()
    output.mkdir(parents=True, exist_ok=True)
    files = ['scripts/test_runtime_codegen.py', 'fixtures/art-runtime/codegen_policy.cpp', 'platform/linux/no_codegen.h']
    record = {'platform': sys.platform, 'architecture': platform.machine(), 'runtime_executed': False,
              'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'project_sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files},
              'cases': {}}
    result_file = output / 'result.json'
    def save():
        result_file.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    binary = output / 'codegen-policy'
    command = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
               '-I', str(ROOT / 'platform/linux'), str(ROOT / files[1]), '-o', str(binary)]
    record['compiler'] = subprocess.check_output([args.cxx, '--version'], text=True).splitlines()[0]
    record['build_command'] = command
    save()
    compiled = subprocess.run(command, capture_output=True, encoding='utf-8')
    (output / 'build.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    if compiled.returncode:
        raise RuntimeError('Codegen guard compilation failed; see ' + str(output / 'build.log'))
    record['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
    modes = ['allow', 'anonymous-exec', 'file-exec', 'protect-exec', 'thread-exec', 'existing-thread-exec',
             'execve', 'execveat', 'shared-exec', 'pkey-exec', 'personality']
    for mode in modes:
        start = time.monotonic()
        process = subprocess.run([str(binary), mode], capture_output=True, encoding='utf-8', timeout=20)
        record['cases'][mode] = {'exit': process.returncode, 'stdout': process.stdout, 'stderr': process.stderr,
                                 'seconds': time.monotonic() - start}
        save()
        if mode == 'allow':
            passed = (process.returncode, process.stdout, process.stderr) == (
                0, 'ARTBox: native code, data mappings and threads remain usable\n', '')
        else:
            passed = process.returncode == 126 and process.stdout == '' and re.fullmatch(
                r'ARTBox: denied executable mapping or process execution\n[0-9]+\n', process.stderr)
        if not passed:
            raise RuntimeError('Codegen guard case failed: ' + mode + '; see ' + str(result_file))
    record['passed'] = len(modes)
    save()
    print(f'ARTBox: {len(modes)} native codegen guard cases passed; no ART VM started')


if __name__ == '__main__': main()
