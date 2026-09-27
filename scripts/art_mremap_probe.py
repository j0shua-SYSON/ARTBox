"""Run the actual ART capability probe with injected syscall outcomes.

These are control-flow tests, not real mmap/mremap compatibility claims.
"""
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'runtime/gc/collector/mark_compact.cc'


def check_probe(original, adapted, output, cxx='c++'):
    output.mkdir(parents=True, exist_ok=True)
    cases = []
    for label, source in [('original', original / SOURCE), ('adapted', adapted / SOURCE)]:
        text = source.read_text(encoding='utf-8')
        start = text.index('static bool HaveMremapDontunmap() {')
        end = text.index('\n}\n', start) + 3
        notice = text[:text.index('*/') + 2]
        directory = output / label
        directory.mkdir(exist_ok=True)
        include = directory / 'mremap_probe.inc'
        include.write_text(notice + '\n' + text[start:end], encoding='utf-8')
        binary = directory / ('probe.exe' if os.name == 'nt' else 'probe')
        command = [cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(directory),
                   str(ROOT / 'fixtures/art-runtime/mremap_probe.cpp'), '-o', str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
        (directory / 'build.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
        compiled.check_returncode()
        for name in ('map-unsupported', 'map-unimplemented', 'map-no-memory',
                     'remap-unsupported', 'remap-no-memory', 'old-unmap-fails', 'new-unmap-fails', 'success'):
            fatal = name in ('map-no-memory', 'old-unmap-fails', 'new-unmap-fails') or (
                label == 'original' and name in ('map-unsupported', 'map-unimplemented'))
            expected = 90 if fatal else 0
            run = subprocess.run([str(binary), name], capture_output=True, text=True, timeout=10)
            row = {'profile': label, 'case': name, 'exit': run.returncode,
                   'stdout': run.stdout, 'stderr': run.stderr, 'expected_exit': expected,
                   'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                   'extracted_sha256': hashlib.sha256(include.read_bytes()).hexdigest(),
                   'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
            cases.append(row)
            output_text = '' if fatal else 'supported\n' if name == 'success' else 'unsupported\n'
            if run.returncode != expected or run.stdout != output_text or run.stderr:
                raise RuntimeError('ART mremap feature probe regression: ' + repr(row))
    return {'scope': 'Actual ART feature probe with injected syscalls; no runtime startup',
            'cases': cases, 'passed': len(cases)}
