"""Record real ARM64 memory/illegal-instruction faults before ABI translation."""
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from environment import environment

binary = Path(sys.argv[1]).resolve()
if sys.platform not in ('linux', 'darwin') or platform.machine().lower() not in ('arm64', 'aarch64'):
    raise RuntimeError('Native ARM64 Linux or Darwin is required')
output = binary.parent / 'signal-faults'
output.mkdir(exist_ok=True)
sources = ['tests/native_signal_faults.c', 'tests/run_signal_faults.py', 'CMakeLists.txt',
           'core/include/artbox/signal_context.h', 'platform/include/artbox/native_signal_context.h', 'LICENSE']
if sys.platform == 'darwin': sources += ['platform/apple/native_signal_context.c']
record = {'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
          'scope': 'Native host fault metadata and context return; no Android handler translation',
          'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'runs': {},
          'platform': sys.platform, 'machine': platform.machine(), 'kernel': platform.release(),
          'project_sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources}}
bundle = output / 'corresponding-source.zip'
with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for name in sources: archive.write(ROOT / name, 'artbox/' + name)
record['source_bundle_sha256'] = hashlib.sha256(bundle.read_bytes()).hexdigest()
(output / 'ARTBOX-LICENSE.txt').write_bytes((ROOT / 'LICENSE').read_bytes())
if sys.platform == 'darwin':
    signed = subprocess.run(['codesign', '--verify', '--strict', '--verbose=2', str(binary)],
                            env=environment(), capture_output=True, text=True, timeout=10)
    (output / 'signature.log').write_text(signed.stdout + signed.stderr, encoding='utf-8')
    signed.check_returncode()
    record['signature_verified'] = True
for mode, option in [('native', None), ('dropped-edit', '--drop-register-edit'),
                     ('dropped-address', '--drop-fault-address')]:
    result = subprocess.run([str(binary)] + ([option] if option else []), env=environment(),
                            capture_output=True, text=True, timeout=10)
    record['runs'][mode] = {'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    if mode == 'native':
        assert result.returncode == 0 and not result.stderr, record['runs'][mode]
        native = json.loads(result.stdout)
        assert native['fault_cases'] == 5 and native['native_resume'] and native['alternate_stack'] and native['x18_preserved']
        assert [row['name'] for row in native['faults']] == [
            'null-read', 'protected-read', 'readonly-write', 'unaligned-atomic', 'undefined-instruction']
        assert all(isinstance(row['esr'], int) and row['code'] > 0 for row in native['faults'])
    else:
        message = ('fault register edits were not resumed\n' if mode == 'dropped-edit'
                   else 'fault address metadata did not match\n')
        assert result.returncode == 1 and not result.stdout and result.stderr == message, record['runs'][mode]
print('Five native ARM64 fault cases resume correctly; dropped register edits and fault addresses are detected')
