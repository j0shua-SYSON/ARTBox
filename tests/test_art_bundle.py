"""Reject incomplete runtime evidence before staging signed device inputs."""
import sys
sys.dont_write_bytecode = True
import copy
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from art_bundle import validate_acceptance


def completed():
    return dict(passed=True, exit=0, runtime_invocation_attempted=True,
                runtime_started=True, dex_executed=True, lifecycle_verified=True,
                console_verified=True, missing_hello_detected=True, missing_hello_exit=1,
                console_drop_detected=True, console_drop_exit=3,
                native=dict(linked_images=15, constructors=38, tls_modules=1,
                            registered_vms=0, cleanup=True, heap_binding_verified=True,
                            runtime_started=True, dex_executed=True, startup_ns=1000,
                            managed_bytes=4096, process_peak_rss_bytes=65536, threads_reaped=8,
                            vm_budget_bytes=2 << 30, reserved_bytes=1600 << 20,
                            bootstrap_window_bytes=65536, managed_window_bytes=1 << 30),
                managed_checks=dict(heap_checksum=6496, exceptions=3, attachments=4,
                                    gc_before=0, gc_after=1),
                thread_state_checks=dict(threads=3, attach_cycles=4, tls_isolated=True,
                                         main_tls_restored=True),
                vm_worker=dict(primordial=False, current_in_stack=True,
                               requested_stack_bytes=4194304, reported_stack_bytes=4214000,
                               guard_bytes=16384))


class Acceptance(unittest.TestCase):
    def test_complete_lifecycle(self):
        validate_acceptance(completed())

    def test_every_observed_phase_is_required(self):
        for key in ('passed', 'runtime_invocation_attempted', 'runtime_started', 'dex_executed',
                    'lifecycle_verified', 'console_verified', 'console_drop_detected', 'missing_hello_detected'):
            for value in (False, 1, None):
                with self.subTest(key=key, value=value):
                    data = completed(); data[key] = value
                    with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_timeout_or_wrong_control_exit(self):
        for key, value in (('timeout', True), ('exit', 1), ('missing_hello_exit', 0), ('console_drop_exit', 0)):
            data = completed(); data[key] = value
            with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_managed_thread_and_cleanup_failures(self):
        for section, key, value in (
                ('native', 'linked_images', 14), ('native', 'registered_vms', 1),
                ('native', 'cleanup', False), ('native', 'heap_binding_verified', False),
                ('native', 'startup_ns', 0), ('native', 'managed_bytes', True),
                ('native', 'constructors', 0), ('native', 'threads_reaped', 0),
                ('managed_checks', 'gc_after', 0), ('managed_checks', 'heap_checksum', 0),
                ('managed_checks', 'exceptions', 2), ('managed_checks', 'attachments', 0),
                ('thread_state_checks', 'tls_isolated', False),
                ('vm_worker', 'primordial', True), ('vm_worker', 'current_in_stack', False),
                ('vm_worker', 'requested_stack_bytes', 0), ('vm_worker', 'guard_bytes', 4214000)):
            with self.subTest(section=section, key=key):
                data = copy.deepcopy(completed()); data[section][key] = value
                with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_missing_sections(self):
        for key in ('native', 'managed_checks', 'thread_state_checks', 'vm_worker'):
            data = completed(); del data[key]
            with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_oversized_or_missing_memory_budget(self):
        for key, value in (('vm_budget_bytes', 32 << 30), ('reserved_bytes', (2 << 30) + 1),
                           ('reserved_bytes', 0), ('reserved_bytes', True),
                           ('bootstrap_window_bytes', 4 << 30), ('managed_window_bytes', 4 << 30)):
            with self.subTest(key=key, value=value):
                data = completed(); data['native'][key] = value
                with self.assertRaises(RuntimeError): validate_acceptance(data)
        for key in ('vm_budget_bytes', 'reserved_bytes', 'bootstrap_window_bytes', 'managed_window_bytes'):
            with self.subTest(missing=key):
                data = completed(); del data['native'][key]
                with self.assertRaises(RuntimeError): validate_acceptance(data)


if __name__ == '__main__': unittest.main()
