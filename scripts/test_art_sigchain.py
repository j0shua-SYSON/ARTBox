"""Run the unchanged pinned AOSP sigchain with the signed-guest caller on Linux ARM64."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import zipfile
from environment import ROOT, environment
from sources import obtain_files


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('aarch64', 'arm64'):
        parser.error('This reference requires native ARM64 Linux')
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/art-sigchain-reference'
    output.mkdir(parents=True, exist_ok=True)
    spec = json.loads((ROOT / 'third_party/art/runtime-sources.json').read_text(encoding='utf-8'))['art-runtime']
    names = {'sigchainlib/sigchain.cc', 'sigchainlib/sigchain.h', 'sigchainlib/log.h', spec['notice']}
    spec = dict(spec, files=[f for f in spec['files'] if f['path'] in names])
    if len(spec['files']) != len(names):
        raise RuntimeError('Incomplete pinned sigchain sources')
    # Keep this small selection separate from the complete runtime cache marker.
    art = obtain_files('art-sigchain', spec, Path(os.environ['ARTBOX_CACHE_DIR']))
    caller = ROOT / 'fixtures/art-runtime/sigchain_check.cpp'
    runner = ROOT / 'fixtures/art-runtime/sigchain_linux.cpp'
    project = [caller, runner, Path(__file__), ROOT / 'LICENSE']
    binary = output / 'sigchain-reference'
    command = list(map(str, [args.compiler, '-std=c++17', '-O2', '-ffixed-x18', '-pthread',
        '-include', 'limits', '-I', art / 'sigchainlib', caller, runner,
        art / 'sigchainlib/sigchain.cc', '-ldl', '-o', binary]))
    record = {'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
              'scope': 'Same-source caller and unchanged AOSP sigchain; native Linux libc, not Bionic',
              'upstream': spec, 'project_sources': {p.relative_to(ROOT).as_posix(): digest(p) for p in project},
              'compile_command': command, 'runs': {}}
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(names): archive.write(art / name, 'upstream/' + name)
        for path in project: archive.write(path, 'artbox/' + path.relative_to(ROOT).as_posix())
    record['source_bundle_sha256'] = digest(bundle)
    try:
        compiled = subprocess.run(command, capture_output=True, timeout=90)
        (output / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
        compiled.check_returncode()
        record['binary_sha256'] = digest(binary)
        for key, options in [('native', []), ('dropped-special', ['--drop-special'])]:
            process = subprocess.run([str(binary), *options], capture_output=True, text=True, timeout=30)
            record['runs'][key] = {'exit': process.returncode, 'stdout': process.stdout, 'stderr': process.stderr}
            if key == 'native':
                process.check_returncode()
                if json.loads(process.stdout) != {'cases': 22, 'special_calls': 2, 'user_calls': 2} or process.stderr:
                    raise RuntimeError('Real sigchain did not complete its registration and forwarding contract')
            elif process.returncode != 1 or process.stdout or process.stderr != 'sigchain contract: -1005\n':
                raise RuntimeError('Removing the special handler did not fail the expected assertion')
    finally:
        (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print('Pinned AOSP sigchain: 22 checks and missing-special-handler control pass on native Linux')


if __name__ == '__main__':
    main()
