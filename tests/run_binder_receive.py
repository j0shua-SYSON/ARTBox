"""Run the real receive-backing provider in a private disposable directory."""
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
        raise SystemExit('Pass the Binder receive test executable')
    os.environ.update(environment())
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='binder-receive-', dir=os.environ['ARTBOX_TEMP_DIR']) as root:
        result = subprocess.run([executable, root], capture_output=True, text=True, timeout=30)
        print(result.stdout + result.stderr, end='')
        if result.returncode == 77 and 'Native receive provider unavailable' in result.stdout and os.name == 'nt':
            return 77
        result.check_returncode()
        if json.loads(result.stdout) != dict(shared_mapping_cases=35, shared_poll_cases=31, native_alias_verified=True,
                                            ownership_controls=True, wait_contract_cases=4, interrupt_epoch_injected=True,
                                            passive_unmap_wakeup=True, mapped_poll_lifetime=True,
                                            wake_observers=True, passed=True):
            raise RuntimeError('Native receive ownership or alias coherence was not verified')
        if any(Path(root).iterdir()):
            raise RuntimeError('Receive backing was not unlinked before publication')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
