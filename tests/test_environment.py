"""Check configurable paths, space handling, and preservation of user settings."""

import sys

sys.dont_write_bytecode = True

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from environment import ROOT, environment


class EnvironmentTest(unittest.TestCase):
    def test_custom_paths_and_child_process(self):
        scratch_root = Path(os.environ.get("ARTBOX_TEMP_DIR", ROOT / ".tmp"))
        scratch_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="paths with spaces ", dir=scratch_root) as scratch:
            base = Path(scratch)
            original = dict(os.environ)
            for key in ("ARTBOX_ISOLATE_HOME", "HOME", "USERPROFILE", "PIP_CACHE_DIR"):
                original.pop(key, None)
            original.update({
                "HOME": str(base / "existing home"),
                "GH_CONFIG_DIR": str(base / "existing gh"),
                "GIT_CONFIG_GLOBAL": str(base / "existing gitconfig"),
                "PIP_CACHE_DIR": str(base / "shared pip cache"),
                "ARTBOX_CACHE_DIR": str(base / "custom cache"),
                "ARTBOX_TEMP_DIR": str(base / "custom temp"),
                "ARTBOX_BUILD_DIR": str(base / "custom build"),
                "ARTBOX_ARTIFACTS_DIR": str(base / "custom artifacts"),
            })
            before = dict(original)
            configured = environment(original)
            self.assertEqual(original, before)
            for key in ("HOME", "GH_CONFIG_DIR", "GIT_CONFIG_GLOBAL", "PIP_CACHE_DIR"):
                self.assertEqual(configured[key], original[key])
            self.assertEqual(configured["TMPDIR"], str((base / "custom temp").resolve()))
            self.assertTrue((base / "custom build").is_dir())
            probe = "import os,tempfile; from pathlib import Path; p=Path(tempfile.gettempdir())/'child.txt'; p.write_text('ok'); print(p)"
            child = subprocess.check_output([sys.executable, "-B", "-c", probe], env=configured, text=True)
            self.assertEqual(Path(child.strip()).parent, (base / "custom temp").resolve())
            isolated = environment(dict(original, ARTBOX_ISOLATE_HOME="1"))
            self.assertEqual(isolated["HOME"], str((base / "custom cache/home").resolve()))
            self.assertEqual(isolated["GH_CONFIG_DIR"], original["GH_CONFIG_DIR"])


if __name__ == "__main__":
    unittest.main()
