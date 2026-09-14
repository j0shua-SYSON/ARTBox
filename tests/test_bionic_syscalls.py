"""Reject syscall-template drift and unsupported argument contracts before building."""
import sys
sys.dont_write_bytecode = True

import os
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from environment import environment
from bionic_syscalls import adapt_generated


class SyscallAdaptation(unittest.TestCase):
    def setUp(self):
        self.call = {"func": "read", "name": "read", "params": ["int", "void*", "size_t"], "aliases": ["read_alias"]}
        self.source = "ENTRY(read)\n    mov     x8, __NR_read\n    svc     #0\n    ret\nEND(read)\nALIAS_SYMBOL(read_alias, read)\n"

    def test_preserves_symbol_and_alias_contract_without_kernel_entry(self):
        adapted = adapt_generated(self.source, [self.call])
        self.assertIn("ALIAS_SYMBOL(read_alias, read)", adapted)
        self.assertNotIn("svc", adapted)
        self.assertEqual(adapted.count("bl      artbox_bionic_syscall"), 1)

    def test_drift_duplicate_entry_and_excess_arguments_fail(self):
        for source in (self.source.replace("svc     #0", "svc     #1"), self.source + self.source):
            with self.subTest(source=source), self.assertRaises(RuntimeError):
                adapt_generated(source, [self.call])
        with self.assertRaises(RuntimeError):
            adapt_generated(self.source, [{**self.call, "params": ["long"] * 7}])
        with self.assertRaises(RuntimeError):
            adapt_generated(self.source, [self.call, self.call])


if __name__ == "__main__":
    os.environ.update(environment())
    unittest.main()
