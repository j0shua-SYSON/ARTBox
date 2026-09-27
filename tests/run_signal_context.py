"""Check native signal return and detect deliberately dropped register edits."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from environment import environment

binary = Path(sys.argv[1]).resolve()
output = binary.parent / 'signal-context'
output.mkdir(exist_ok=True)
record = {'project_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
          'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'runs': {},
          'project_sources': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in (
              'core/include/artbox/signal_context.h', 'core/src/signal_context.c',
              'tests/native_signal_context_linux.c', 'tests/run_signal_context.py')}}
for mode in ('native', 'dropped-edit'):
    command = [str(binary)] + (['--drop-register-edit'] if mode == 'dropped-edit' else [])
    result = subprocess.run(command, env=environment(), capture_output=True, text=True, timeout=10)
    record['runs'][mode] = {'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    if mode == 'native':
        assert result.returncode == 0 and not result.stderr, record['runs'][mode]
        assert json.loads(result.stdout) == {'frame_bytes': 4560, 'native_resume': True,
            'general_register_edit': True, 'vector_edit': True, 'x18_preserved': True}
    else:
        assert result.returncode == 1 and not result.stdout, record['runs'][mode]
        assert result.stderr == 'handler register edits were not resumed\n', record['runs'][mode]
print('Native Linux handler resumes edited PC, x0 and v0; x18 is preserved; dropped edits are detected')
