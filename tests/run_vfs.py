"""Run rooted-file contracts using disposable project-local fixtures."""
import sys
sys.dont_write_bytecode = True
import os
from pathlib import Path
import subprocess
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from environment import environment
os.environ.update(environment())
with tempfile.TemporaryDirectory(prefix="vfs-", dir=os.environ["TMPDIR"]) as directory:
    base = Path(directory)
    root = base / "root"
    (root / "data").mkdir(parents=True)
    (root / "system").mkdir()
    (root / "system/readonly").write_bytes(b"system")
    guard = base / "guard"
    guard.write_bytes(b"must remain unchanged")
    if os.name != "nt":
        (root / "data/link").symlink_to(guard)
        (root / "data/jump").symlink_to(base, target_is_directory=True)
    subprocess.run([sys.argv[1], str(root)], check=True, timeout=15)
    if guard.read_bytes() != b"must remain unchanged":
        raise RuntimeError("Guest path escaped its root")
