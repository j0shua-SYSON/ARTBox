"""Reject nominal success, wrong controls, missing execution and leaked owners."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import copy
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from looper_guest import EXPECTED, verify_result


class LooperEvidence(unittest.TestCase):
    def fixture(self, name='normal'):
        return dict(looper=copy.deepcopy(EXPECTED[name]), constructors=8, tls_modules=1,
                    linked_images=5, registered_vms=0, heap_binding_verified=True,
                    sigchain_cases=22, sigchain_mutation=-1005, runtime_started=False,
                    dex_executed=False, threads_reaped=1, cleanup=True,
                    looper_check_ns=2, load_relocate_ns=3, bootstrap_ns=4)

    def test_accepts_only_the_expected_normal_and_negative_results(self):
        for name in EXPECTED:
            expected_exit = 0 if name == 'normal' else 1
            result = self.fixture(name)
            self.assertEqual(verify_result(name, expected_exit, result), result)
            with self.assertRaises(ValueError): verify_result(name, 1 - expected_exit, result)
        with self.assertRaises(ValueError): verify_result('other', 0, self.fixture())

    def test_every_shared_observation_is_required(self):
        for name in EXPECTED:
            for key in EXPECTED[name]:
                with self.subTest(control=name, key=key):
                    result = self.fixture(name)
                    result['looper'][key] += 1
                    with self.assertRaises(ValueError): verify_result(name, int(name != 'normal'), result)
        result = self.fixture(); del result['looper']
        with self.assertRaises(ValueError): verify_result('normal', 0, result)
        result = self.fixture(); result['looper']['wake_threads'] = True
        with self.assertRaises(ValueError): verify_result('normal', 0, result)

    def test_controls_cannot_be_swapped_or_replaced_by_success(self):
        for name in ('retain-callback', 'omit-wake'):
            with self.assertRaises(ValueError): verify_result(name, 1, self.fixture())
        with self.assertRaises(ValueError): verify_result('retain-callback', 1, self.fixture('omit-wake'))

    def test_leaked_threads_wrong_image_profile_and_missing_measurements_fail(self):
        changes = dict(tls_modules=2, linked_images=15, registered_vms=1,
                       heap_binding_verified=False, sigchain_cases=0, sigchain_mutation=0,
                       runtime_started=True, dex_executed=True, threads_reaped=0, cleanup=False,
                       constructors=0, looper_check_ns=0, load_relocate_ns=-1, bootstrap_ns=None)
        for key, value in changes.items():
            with self.subTest(key=key):
                result = self.fixture(); result[key] = value
                with self.assertRaises(ValueError): verify_result('normal', 0, result)
                del result[key]
                with self.assertRaises(ValueError): verify_result('normal', 0, result)


if __name__ == '__main__': unittest.main()
