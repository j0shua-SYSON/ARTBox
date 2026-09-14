"""Require real hardware protection faults, independently of VM metadata."""
import sys
sys.dont_write_bytecode = True

import subprocess

for mode in ("readonly", "none", "hole"):
    result = subprocess.run([sys.argv[1], mode], capture_output=True, text=True, timeout=10)
    if result.returncode != 77 or result.stdout.strip() != "fault check armed" or result.stderr:
        raise RuntimeError(f"{mode}: expected memory fault was not caught: {result.returncode}, {result.stdout!r}, {result.stderr!r}")
print("Hardware rejected readonly writes, PROT_NONE reads and reads from a partial-unmap hole")
