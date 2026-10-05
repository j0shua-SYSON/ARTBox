"""Execute the exact Android parser caller on native Linux ARM64."""
# SPDX-License-Identifier: MIT
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
from environment import environment
from kernel_config_guest import digest, verify_build, verify_contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-dir', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        parser.error('The reference requires native Linux ARM64; no CPU emulation')
    os.environ.update(environment())
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/kernel-config-reference').resolve()
    output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip()
    if subprocess.check_output(['git','status','--porcelain']).strip():
        raise RuntimeError('Linux parser reference requires a clean revision')
    native = args.native_dir.resolve()
    build = verify_build(native, revision)
    binary = native / 'linux-reference'
    binary.chmod(0o755)
    started = time.monotonic_ns()
    process = subprocess.run([str(binary)], capture_output=True, timeout=30)
    elapsed = time.monotonic_ns() - started
    (output / 'stdout').write_bytes(process.stdout)
    (output / 'stderr').write_bytes(process.stderr)
    result = dict(project_commit=revision, build_result_sha256=digest(native / 'result.json'),
                  objects=build['objects'], reference_sha256=digest(binary),
                  source_bundle_sha256=build['source_bundle_sha256'], exit=process.returncode,
                  process_ns=elapsed, native_linux_execution_verified=False,
                  stdout_sha256=hashlib.sha256(process.stdout).hexdigest(),
                  stderr_sha256=hashlib.sha256(process.stderr).hexdigest())
    try:
        result['native'] = json.loads(process.stdout)
        verify_contract(process.returncode, result['native'])
        result['native_linux_execution_verified'] = True
    finally:
        (output / 'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Native Linux ARM64: original parser passes 89 cases and controls -104/-105')


if __name__ == '__main__': main()
