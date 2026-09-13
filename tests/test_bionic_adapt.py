"""Check exact source overlays and the compiled-instruction gate without downloads."""
import sys

sys.dont_write_bytecode = True

import hashlib
import os
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from environment import environment
from bionic_adapt import adapt_sources, check_native, inventory, stack_references


class Adaptation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.overlay = self.root / "overlay"
        self.name = "libc/platform/bionic/tls.h"
        self.original = b"/* original license */\nold\n"
        path = self.source / self.name
        path.parent.mkdir(parents=True)
        path.write_bytes(self.original)
        (path.parent / "adjacent.h").write_text("adjacent include\n", encoding="utf-8")
        self.patch = {"files": [{"path": self.name, "sha256": hashlib.sha256(self.original).hexdigest(),
                                "replacements": [{"before": "old\n", "after": "adapted\n"}]}]}

    def test_preserves_source_notice_and_relative_includes(self):
        records = adapt_sources(self.source, self.overlay, self.patch)
        self.assertEqual((self.source / self.name).read_bytes(), self.original)
        self.assertEqual((self.overlay / self.name).read_bytes(), b"/* original license */\nadapted\n")
        self.assertEqual((self.overlay / "libc/platform/bionic/adjacent.h").read_text(), "adjacent include\n")
        self.assertEqual(records[0]["upstream_sha256"], hashlib.sha256(self.original).hexdigest())
        self.assertEqual(records[0]["adapted_sha256"], hashlib.sha256((self.overlay / self.name).read_bytes()).hexdigest())

    def test_drift_and_ambiguous_edits_leave_no_partial_overlay(self):
        for before, digest in (("absent", self.patch["files"][0]["sha256"]), ("old\n", "0" * 64)):
            with self.subTest(before=before, digest=digest):
                self.patch["files"][0]["sha256"] = digest
                self.patch["files"][0]["replacements"][0]["before"] = before
                with self.assertRaises(RuntimeError):
                    adapt_sources(self.source, self.overlay, self.patch)
                self.assertFalse(self.overlay.exists())
        self.patch["files"][0]["sha256"] = hashlib.sha256(self.original).hexdigest()
        self.patch["files"][0]["replacements"][0]["before"] = "old\n"
        self.patch["files"].append({"path": "missing.cpp", "sha256": "0" * 64, "replacements": []})
        with self.assertRaises(RuntimeError):
            adapt_sources(self.source, self.overlay, self.patch)
        self.assertFalse(self.overlay.exists())

    def test_rejects_source_overlap_and_path_escape(self):
        with self.assertRaises(RuntimeError):
            adapt_sources(self.source, self.source / "overlay", self.patch)
        for path in ("../escape", "/escape", "x/../../escape"):
            self.patch["files"][0]["path"] = path
            with self.assertRaises(RuntimeError):
                adapt_sources(self.source, self.overlay, self.patch)
        self.assertEqual((self.source / self.name).read_bytes(), self.original)

    def test_instruction_gate_has_a_positive_control(self):
        ordinary = "path/x18/fixture.o:\n0000 <x28>:\n 0: ldr x8, [x0]\n 4: ret\n"
        symbols = [{"symbol": name} for name in ("artbox_bionic_get_tls", "artbox_bionic_set_tls", "artbox_bionic_syscall")]
        protection = {"failure_branches": 2, "guard_address_relocations": 4}
        check_native(inventory(ordinary), symbols, protection)
        for instruction in ("mrs x8, TPIDR_EL0", "msr TPIDR_EL0, x0", "mrs x2, TPIDRRO_EL0",
                            "mov x18, x1", "str w27, [sp]", "mov x28, x0", "svc #0", "<unknown>"):
            with self.subTest(instruction=instruction):
                with self.assertRaises(RuntimeError):
                    check_native(inventory(ordinary + " 8: " + instruction + "\n"), symbols, protection)
        with self.assertRaises(RuntimeError):
            check_native(inventory(""), symbols, protection)
        with self.assertRaises(RuntimeError):
            check_native(inventory(ordinary), symbols + [{"symbol": "__aarch64_cas4_acq"}], protection)
        for missing in protection:
            with self.subTest(missing=missing), self.assertRaises(RuntimeError):
                check_native(inventory(ordinary), symbols, {**protection, missing: 0})

    def test_stack_protection_requires_code_references_not_an_undefined_name(self):
        def relocation(kind, symbol):
            return {"Relocation": {"Type": {"Name": kind}, "Symbol": {"Name": symbol}}}
        refs = [{"Relocs": [relocation("R_AARCH64_CALL26", "__stack_chk_fail"),
                             relocation("R_AARCH64_JUMP26", "__stack_chk_fail"),
                             relocation("R_AARCH64_ADR_GOT_PAGE", "__stack_chk_guard"),
                             relocation("R_AARCH64_LD64_GOT_LO12_NC", "__stack_chk_guard"),
                             relocation("R_AARCH64_ABS64", "__stack_chk_fail"),
                             relocation("R_AARCH64_CALL26", "unrelated")]}]
        self.assertEqual(stack_references(refs), {"failure_branches": 2, "guard_address_relocations": 2})
        self.assertEqual(stack_references([]), {"failure_branches": 0, "guard_address_relocations": 0})


if __name__ == "__main__":
    os.environ.update(environment())
    unittest.main()
