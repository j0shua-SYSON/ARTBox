import sys
sys.dont_write_bytecode = True
from pathlib import Path
import hashlib
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from art_native_tls import adapt_assembly


class NativeRuntimeTLS(unittest.TestCase):
    def setUp(self):
        self.source = b'controlled source\n'
        self.rule = {'source_sha256': hashlib.sha256(self.source).hexdigest(),
                     'tls_symbols': ['sampler'], 'tlsdesc_calls': 1, 'tp_reads_replaced': 1}
        self.assembly = '.tlsdesccall sampler\nblr x1\nmrs x8, TPIDR_EL0\nadd x0, x8, x0\n'

    def test_controlled_sampler_uses_absolute_descriptor(self):
        changed, edits = adapt_assembly(self.source, self.assembly, self.rule)
        self.assertIn('mov\tx8, xzr', changed)
        self.assertNotIn('TPIDR_EL0', changed)
        self.assertEqual(edits, {'tlsdesc_calls': 1, 'tp_reads_replaced': 1})

    def test_changed_source_cannot_reuse_review(self):
        with self.assertRaises(ValueError):
            adapt_assembly(self.source + b'changed', self.assembly, self.rule)

    def test_other_tls_variable_or_extra_access_requires_review(self):
        for assembly in (self.assembly.replace('sampler', 'another'),
                         self.assembly + '.tlsdesccall sampler\nmrs x9, TPIDR_EL0\n',
                         self.assembly + 'mrs x9, TPIDR_EL0\n'):
            with self.assertRaises(ValueError):
                adapt_assembly(self.source, assembly, self.rule)

    def test_foreign_thread_pointer_and_local_exec_rejected(self):
        for tail in ('msr TPIDR_EL0, x0\n', 'mrs x0, TPIDRRO_EL0\n',
                     'add x0, x0, :tprel_hi12:sampler\n'):
            with self.assertRaises(ValueError):
                adapt_assembly(self.source, self.assembly + tail, self.rule)


if __name__ == '__main__': unittest.main()
