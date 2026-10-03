import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from icu_guest_link import check_code, check_dependencies


class ICUGuestBoundary(unittest.TestCase):
    def setUp(self):
        self.images = {
            'client': {'needed': ['icu'], 'exports': [], 'imports': {'convert': 'U', 'optional': 'w'}},
            'icu': {'needed': ['libc'], 'exports': ['convert'], 'imports': {'malloc': 'U'}},
            'libc': {'needed': [], 'exports': ['malloc'], 'imports': {}},
            'unrelated': {'needed': [], 'exports': ['missing'], 'imports': {}}}

    def test_transitive_needed_scope(self):
        self.assertEqual(check_dependencies(self.images, ['client', 'icu']),
                         {'client': ['client', 'icu', 'libc'], 'icu': ['icu', 'libc']})

    def test_unreachable_provider_cannot_satisfy_import(self):
        self.images['client']['imports']['missing'] = 'U'
        with self.assertRaisesRegex(ValueError, 'missing'):
            check_dependencies(self.images, ['client'])

    def test_missing_needed_library_is_rejected_even_without_imports(self):
        self.images['icu']['needed'].append('absent')
        with self.assertRaisesRegex(ValueError, 'absent'):
            check_dependencies(self.images, ['client'])

    def test_cycles_terminate_and_keep_symbol_scope(self):
        self.images['libc']['needed'] = ['icu']
        self.assertEqual(check_dependencies(self.images, ['client'])['client'], ['client', 'icu', 'libc'])

    def test_no_kernel_thread_pointer_or_reserved_register_use(self):
        self.assertEqual(check_code('0000 <f>:\n 0: ret')['instruction_count'], 1)
        for instruction in ('svc #0', 'mrs x0, TPIDR_EL0', 'msr TPIDR_EL0, x0',
                            'mov x18, x0', 'str w27, [x0]', 'ldr x28, [x0]', '<unknown>'):
            with self.subTest(instruction=instruction), self.assertRaises(ValueError):
                check_code('0000 <f>:\n 0: ' + instruction)
        with self.assertRaises(ValueError): check_code('')


if __name__ == '__main__': unittest.main()
