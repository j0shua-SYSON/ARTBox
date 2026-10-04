"""Run native alias ownership and fault checks without crash/core artifacts."""
import sys
sys.dont_write_bytecode = True

import os
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import environment


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Pass the native mapping test executable')
    os.environ.update(environment())
    executable = str(Path(sys.argv[1]).resolve())
    logs = []
    for mode in ('lifetime', 'write-fault'):
        with tempfile.TemporaryDirectory(prefix='binder-alias-', dir=os.environ['ARTBOX_TEMP_DIR']) as root:
            result = subprocess.run([executable, root, mode], capture_output=True, text=True, timeout=15)
            output = result.stdout + result.stderr
            print(output, end='')
            logs.append(output)
            if mode == 'lifetime' and result.returncode == 77 and 'Native file provider unavailable;' in output:
                return 77
            expected = 0 if mode == 'lifetime' else 77
            if result.returncode != expected or (mode == 'write-fault' and 'receive alias write fault armed' not in output):
                raise RuntimeError(f'Native Binder alias {mode} did not satisfy its contract: exit {result.returncode}')
            if any(Path(root).iterdir()):
                raise RuntimeError('Receive backing file was not unlinked before use')
    (Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / 'm4-binder-mapping.log').write_text(''.join(logs), encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
