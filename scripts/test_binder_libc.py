"""Execute the exact NDK caller against original Bionic on native Linux ARM64."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import zipfile

from binder_libc import digest, EXPECTED, SIGNED_EXPECTED
from environment import ROOT, environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root', type=Path, required=True)
    args = parser.parse_args()
    os.environ.update(environment())
    if sys.platform != 'linux' or platform.machine().lower() not in ('aarch64', 'arm64'):
        parser.error('Native Linux ARM64 is required; no CPU emulation is used')
    evidence = args.evidence_root.resolve()
    producer = evidence / 'artifacts/m2-bionic-startup.json'
    report = json.loads(producer.read_text(encoding='utf-8'))
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    metadata = report['binder_libc']
    if (report['project_commit'] != head or metadata['expected'] != EXPECTED or
            metadata['source_sha256'] != digest(ROOT / 'fixtures/binder-libc/check.c') or
            any(report[mode].get(k) != value for mode in ('native', 'sampled_native')
                for k, value in SIGNED_EXPECTED.items())):
        raise RuntimeError('Both signed Bionic modes must pass this revision and its two controls')
    inputs = evidence / 'build/m2/bionic-startup'
    caller = inputs / 'binder-libc-check.o'
    reference = inputs / 'binder-libc-reference'
    manifest = reference / 'build.json'
    if digest(caller) != metadata['object_sha256'] or digest(manifest) != metadata['reference_manifest_sha256']:
        raise RuntimeError('Binder libc caller or reference metadata differs from the producer')
    build = json.loads(manifest.read_text(encoding='utf-8'))
    pin = json.loads((ROOT / 'third_party/bionic/binder-libc.json').read_text(encoding='utf-8'))
    if build['pin'] != pin or build['expected'] != EXPECTED or build['caller_sha256'] != digest(caller):
        raise RuntimeError('Binder libc native reference pin or caller differs')
    for name, expected in build['objects'].items():
        if Path(name).name != name or digest(reference / name) != expected:
            raise RuntimeError('Binder libc original object changed')
    bundle = reference / 'corresponding-source.zip'
    if digest(bundle) != build['source_bundle_sha256']:
        raise RuntimeError('Binder libc source archive changed')
    with zipfile.ZipFile(bundle) as archive:
        import hashlib
        for name, expected in build['source_files'].items():
            data = archive.read(name)
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError('Binder libc source bytes changed: ' + name)
            if name.startswith('artbox/') and data != (ROOT / name[len('artbox/'):]).read_bytes():
                raise RuntimeError('Binder libc project source differs from this checkout: ' + name)
        for name, expected in pin['files'].items():
            if build['source_files'].get(name) != expected:
                raise RuntimeError('Binder libc archive lacks its pinned source or notice')
    binary = reference / 'binder-libc-linux'
    if digest(binary) != build['executable_sha256']:
        raise RuntimeError('Binder libc reference executable changed')
    binary.chmod(binary.stat().st_mode | 0o100)
    output = Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/binder-libc-reference'
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic_ns()
    process = subprocess.run([str(binary)], capture_output=True, timeout=30)
    elapsed = time.monotonic_ns() - started
    (output / 'reference.log').write_bytes(process.stdout + process.stderr)
    process.check_returncode()
    observed = json.loads(process.stdout)
    if observed != EXPECTED:
        raise RuntimeError('Original Linux/Bionic reference differs from signed Bionic')
    result = dict(project_commit=head, native_linux=observed, signed_bionic_modes=['native', 'sampled_native'],
                  elapsed_process_ns=elapsed, producer_sha256=digest(producer), caller_sha256=digest(caller),
                  executable_sha256=digest(binary), source_bundle_sha256=digest(bundle), pin=pin,
                  scope='36 C-locale pattern cases, 14 realtime/monotonic/error checks, two deliberate controls',
                  physical_execution_verified=False)
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR']); artifacts.mkdir(parents=True, exist_ok=True)
    (artifacts / 'm4-binder-libc-linux.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Original Linux/Bionic and both signed Bionic profiles pass 50 libc cases and both controls')


if __name__ == '__main__': main()
