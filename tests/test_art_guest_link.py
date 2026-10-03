import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from art_guest_link import check_code, check_imports, SNAPSHOTS, HOST_IMPORTS


class GuestLinkBoundary(unittest.TestCase):
    def setUp(self):
        self.code = '\n'.join('0000 <' + name + '>:\n  0: stp x18, x19, [x0, #0x90]' for name in SNAPSHOTS)

    def test_reviewed_register_snapshots_do_not_modify_platform_register(self):
        result = check_code(self.code)
        self.assertEqual(result['inventory']['x18_mentions'], 3)
        self.assertEqual(len(result['platform_register_reads']), 3)

    def test_register_writes_and_unreviewed_reads_fail(self):
        for code in (self.code.replace('stp x18, x19', 'ldp x18, x19'),
                     self.code.replace('[x0, #0x90]', '[x18, #0x90]'),
                     self.code.replace('[x0, #0x90]', '[x0, #0x90]!'),
                     self.code.replace(next(iter(SNAPSHOTS)), 'unknown_function'),
                     self.code + '\n 4: mov x18, x0'):
            with self.assertRaises(ValueError): check_code(code)

    def test_kernel_thread_pointer_and_unknown_opcodes_fail(self):
        for instruction in ('svc #0', 'mrs x0, TPIDR_EL0', 'msr TPIDR_EL0, x0', '<unknown>'):
            with self.assertRaises(ValueError): check_code(self.code + '\n 4: ' + instruction)

    def test_only_explicit_host_imports_remain(self):
        imports = {name: 'U' for name in HOST_IMPORTS}
        imports.update({'malloc': 'U', 'sin': 'U', 'optional': 'w'})
        self.assertEqual(check_imports(imports, {'malloc', 'sin'}), sorted(HOST_IMPORTS))
        with self.assertRaises(ValueError): check_imports(imports, {'malloc'})
        imports['unreviewed_host_call'] = 'U'
        with self.assertRaises(ValueError): check_imports(imports, {'malloc', 'sin'})


if __name__ == '__main__': unittest.main()
