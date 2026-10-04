"""Check native signal return and detect deliberately dropped register edits."""
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from environment import environment

binary = Path(sys.argv[1]).resolve()
if sys.platform not in ('linux', 'darwin') or platform.machine().lower() not in ('arm64', 'aarch64'):
    raise RuntimeError('Native ARM64 Linux or Darwin is required')
output = binary.parent / 'signal-context'
output.mkdir(exist_ok=True)
sources = ['core/include/artbox/signal_context.h', 'core/src/signal_context.c', 'tests/run_signal_context.py']
sources += (['tests/native_signal_context_linux.c'] if sys.platform == 'linux' else [
    'tests/native_signal_context_apple.c', 'platform/apple/native_signal_context.c',
    'platform/include/artbox/native_signal_context.h', 'platform/apple/native_signal_binding.c',
    'platform/include/artbox/native_signal_binding.h', 'platform/native_syscall.c', 'platform/native_tls.c',
    'platform/include/artbox/native_syscall.h', 'platform/include/artbox/native_tls.h',
    'core/src/vm.cpp', 'core/include/artbox/vm.h', 'platform/native_vm.c', 'platform/include/artbox/native_vm.h'])
record = {'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
          'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'runs': {},
          'platform': sys.platform, 'machine': platform.machine(),
          'project_sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources}}
if sys.platform == 'darwin':
    signature = subprocess.run(['codesign', '--verify', '--strict', '--verbose=2', str(binary)],
                               env=environment(), capture_output=True, text=True, timeout=10)
    (output / 'signature.log').write_text(signature.stdout + signature.stderr, encoding='utf-8')
    signature.check_returncode()
    record['signature_verified'] = True
for mode in ('native', 'dropped-edit'):
    command = [str(binary)] + (['--drop-register-edit'] if mode == 'dropped-edit' else [])
    result = subprocess.run(command, env=environment(), capture_output=True, text=True, timeout=10)
    record['runs'][mode] = {'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    if mode == 'native':
        assert result.returncode == 0 and not result.stderr, record['runs'][mode]
        expected = {'frame_bytes': 4560, 'native_resume': True,
                    'general_register_edit': True, 'vector_edit': True, 'x18_preserved': True}
        if sys.platform == 'darwin':
            expected.update(signal_binding=True, mapper_lock_held=True, vm_fault_snapshot=True)
        assert json.loads(result.stdout) == expected
    else:
        assert result.returncode == 1 and not result.stdout, record['runs'][mode]
        assert result.stderr == 'handler register edits were not resumed\n', record['runs'][mode]
name = 'Linux' if sys.platform == 'linux' else 'Darwin'
print('Native ' + name + ' handler resumes edited PC, x0 and v0; x18 is preserved; dropped edits are detected')
