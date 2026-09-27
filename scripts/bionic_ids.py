"""Generate Android's account table with its pinned upstream build tool."""
import hashlib
import json
import os
import subprocess
import sys

from environment import ROOT
from sources import obtain


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(build, run_tests):
    pin = json.loads((ROOT / "third_party/bionic/android-ids.json").read_text(encoding="utf-8"))
    sources = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))
    generator = obtain(pin["generator"])
    header_root = obtain(pin["header_component"])
    cutils = obtain("bionic-libcore-headers")
    directory = build / "android-ids"
    directory.mkdir(parents=True, exist_ok=True)
    inputs = {}
    for name, source in ((pin["generator"], generator), ("bionic-libcore-headers", cutils)):
        for item in sources[name]["files"]:
            path = directory / "inputs" / name / item["path"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((source / item["path"]).read_bytes())
            inputs[name + "/" + item["path"]] = digest(path)
    header = header_root / pin["header"]
    retained = directory / "inputs" / pin["header_component"] / pin["header"]
    retained.parent.mkdir(parents=True, exist_ok=True)
    retained.write_bytes(header.read_bytes())
    inputs[pin["header_component"] + "/" + pin["header"]] = digest(retained)
    tested = False
    if run_tests and os.name != "nt":
        # The unchanged tests reopen NamedTemporaryFile paths while still open,
        # a POSIX behavior. Actual generation and its output hash run everywhere.
        result = subprocess.run([sys.executable, "-B", str(generator / "tools/fs_config/test_fs_config_generator.py")],
                                capture_output=True, timeout=60)
        (directory / "upstream-tests.log").write_bytes(result.stdout + result.stderr)
        if result.returncode:
            sys.stderr.buffer.write(result.stdout + result.stderr)
            result.check_returncode()
        tested = True
    raw = subprocess.check_output([sys.executable, "-B", str(generator / "tools/fs_config/fs_config_generator.py"),
                                   pin["command"], str(header)])
    # Python's stdout newline translation follows the host. Keep one canonical
    # source representation without changing the upstream generator or table.
    generated = raw.replace(b"\r\n", b"\n")
    if hashlib.sha256(generated).hexdigest() != pin["output_sha256"]:
        raise RuntimeError("The generated Android account table differs from its reviewed output")
    (directory / "generated_android_ids.h").write_bytes(generated)
    # Package declarations carry the tool's notices; libcutils/NOTICE supplies
    # the complete Apache-2.0 terms alongside its header's notice.
    notice = bytearray()
    for name, relative in ((pin["generator"], "Android.bp"),
                           (pin["generator"], "tools/fs_config/Android.bp"),
                           (pin["header_component"], pin["header"]),
                           ("bionic-libcore-headers", "libcutils/include/cutils/misc.h"),
                           ("bionic-libcore-headers", "libcutils/NOTICE")):
        notice.extend(f"\n--- {name}/{relative} ---\n".encode("utf-8"))
        notice.extend((directory / "inputs" / name / relative).read_bytes())
    notice_path = build / "FSCONFIG-NOTICE.txt"
    notice_path.write_bytes(notice)
    return directory, cutils, {"pin": pin, "inputs": inputs, "upstream_tests_executed": tested,
                               "raw_output_sha256": hashlib.sha256(raw).hexdigest(),
                               "notice_sha256": digest(notice_path), "output_sha256": pin["output_sha256"]}
