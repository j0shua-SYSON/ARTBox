"""Reject parser success without controls, ownership or identical reference inputs."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import copy
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from kernel_config_guest import EXPECTED, verify_contract, verify_guest, verify_reference


class ParserEvidence(unittest.TestCase):
    def fixture(self):
        return dict(kernel_config=EXPECTED.copy(), constructors=8, tls_modules=1,
                    linked_images=5, registered_vms=0, heap_binding_verified=True,
                    sigchain_cases=22, sigchain_mutation=-1005, runtime_started=False,
                    dex_executed=False, threads_reaped=0, cleanup=True,
                    kernel_config_check_ns=2, load_relocate_ns=3, bootstrap_ns=4)

    def test_positive_and_both_controls_required(self):
        verify_contract(0, EXPECTED)
        verify_guest(0, self.fixture())
        for key in EXPECTED:
            result = EXPECTED.copy(); result[key] = 0
            with self.assertRaises(ValueError): verify_contract(0, result)
            del result[key]
            with self.assertRaises(ValueError): verify_contract(0, result)
        for code in (1, -11, False, None):
            with self.assertRaises(ValueError): verify_contract(code, EXPECTED)
        with self.assertRaises(ValueError): verify_contract(0, None)

    def test_ownership_and_timing_are_required(self):
        for key in self.fixture():
            value = self.fixture(); del value[key]
            with self.assertRaises(ValueError): verify_guest(0, value)
        for key,value in dict(registered_vms=1, linked_images=15, threads_reaped=1,
                              dex_executed=True, cleanup=1, constructors=False,
                              kernel_config_check_ns=0).items():
            result=self.fixture(); result[key]=value
            with self.assertRaises(ValueError): verify_guest(0, result)

    def test_reference_binds_exact_objects_binary_source_and_revision(self):
        build=dict(project_commit='a'*40, objects={'check.o':'b'*64, 'regex.cpp.o':'c'*64},
                   reference_sha256='d'*64, source_bundle_sha256='e'*64)
        reference=dict(**copy.deepcopy(build), build_result_sha256='f'*64, exit=0,
                       native=EXPECTED.copy(), native_linux_execution_verified=True)
        verify_reference(reference,build,'f'*64)
        for key in reference:
            broken=copy.deepcopy(reference); del broken[key]
            with self.assertRaises(ValueError): verify_reference(broken,build,'f'*64)
        for key in ('check.o','regex.cpp.o'):
            broken=copy.deepcopy(reference); broken['objects'][key]='0'*64
            with self.assertRaises(ValueError): verify_reference(broken,build,'f'*64)


if __name__ == '__main__': unittest.main()
