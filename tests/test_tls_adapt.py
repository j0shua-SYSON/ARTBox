import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from tls_adapt import adapt


class TlsAdapt(unittest.TestCase):
    def test_preserves_descriptor_access_and_changes_only_tp_read(self):
        source = '\t.tlsdesccall variable\n\tblr x1\n\tmrs x8, TPIDR_EL0\n\tadd x0, x8, x0\n'
        changed, record = adapt(source)
        self.assertEqual(record, {"tlsdesc_calls": 1, "tp_reads_replaced": 1})
        self.assertIn('.tlsdesccall variable\n\tblr x1', changed)
        self.assertIn('mov\tx8, xzr', changed)
        self.assertTrue(changed.endswith('\tadd x0, x8, x0\n'))

    def test_rejects_unexplained_and_other_models(self):
        for source in ('mrs x8, TPIDR_EL0\n', '.tlsdesccall x\nmsr TPIDR_EL0, x0\n',
                       '.tlsdesccall x\nmrs x18, TPIDR_EL0\n',
                       '.tlsdesccall x\nmrs x8, TPIDR_EL0\nadd x0, x0, :tprel_hi12:x\n'):
            with self.assertRaises(ValueError): adapt(source)


if __name__ == '__main__': unittest.main()
