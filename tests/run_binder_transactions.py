"""Require shared transaction semantics through native-backed guest VFS."""
import sys
sys.dont_write_bytecode = True
import json
import os
from pathlib import Path
import subprocess
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from environment import environment


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Pass the Binder transaction test executable')
    os.environ.update(environment())
    with tempfile.TemporaryDirectory(prefix='binder-transactions-', dir=os.environ['ARTBOX_TEMP_DIR']) as root:
        result = subprocess.run([str(Path(sys.argv[1]).resolve()), root], capture_output=True, text=True, timeout=30)
        print(result.stdout + result.stderr, end='')
        if result.returncode == 77 and 'Native transaction provider unavailable' in result.stdout and os.name == 'nt':
            return 77
        result.check_returncode()
        if json.loads(result.stdout) != dict(same_pid_rejected=True, shared_threaded_ping_pong=True,
                                            death_cases=3, object_handle_lifecycle=True,
                                            native_aliases=True, cleanup=True, passed=True):
            raise RuntimeError('Guest Binder transaction acceptance was incomplete')
        if any(Path(root).iterdir()):
            raise RuntimeError('Binder transaction backing file remains named')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
