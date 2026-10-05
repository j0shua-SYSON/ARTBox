"""Incomplete Binder runtime evidence must never become a device artifact."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from service_bundle import validate_acceptance


def completed():
    executions = {}
    for label, access in (('access', 20), ('wrong-uid', -109), ('wrong-pid', -107), ('roles', 0)):
        roles = label == 'roles'
        executions[label] = dict(exit=0, native=dict(access_cases=access, policy_cases=5,
            provider=32 if roles else 0, client=32 if roles else 0, roles=3 if roles else 1,
            independent_images=18 if roles else 6, manager_retained=roles, finite_roles_cleaned=True,
            passed=True, elapsed_ns=10000, peak_rss_bytes=4096))
    return dict(guest_execution_verified=True, working_tree_dirty=False, executions=executions)


class ServiceAcceptance(unittest.TestCase):
    def test_all_observations(self):
        validate_acceptance(completed())

    def test_failed_or_missing_process(self):
        for label in completed()['executions']:
            for change in ('missing', 'failed', 'timeout', 'no-output'):
                with self.subTest(label=label, change=change):
                    data = completed()
                    if change == 'missing': del data['executions'][label]
                    elif change == 'failed': data['executions'][label]['exit'] = 1
                    elif change == 'timeout': data['executions'][label]['timeout'] = True
                    else: del data['executions'][label]['native']
                    with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_missing_or_bypassed_policy_controls(self):
        for label, access in (('access', 19), ('wrong-uid', 20), ('wrong-pid', 20)):
            data = completed(); data['executions'][label]['native']['access_cases'] = access
            with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_incomplete_role_lifecycle_and_identity(self):
        for key, value in (('provider', 31), ('client', 0), ('roles', 1), ('independent_images', 6),
                           ('manager_retained', False), ('finite_roles_cleaned', False), ('passed', False),
                           ('policy_cases', 4), ('access_cases', 20)):
            with self.subTest(key=key):
                data = completed(); data['executions']['roles']['native'][key] = value
                with self.assertRaises(RuntimeError): validate_acceptance(data)

    def test_strict_types_and_measurements(self):
        for key, value in (('passed', 1), ('finite_roles_cleaned', 1), ('access_cases', False),
                           ('elapsed_ns', 0), ('peak_rss_bytes', True)):
            data = completed(); data['executions']['roles']['native'][key] = value
            with self.assertRaises(RuntimeError): validate_acceptance(data)
        for key, value in (('guest_execution_verified', 1), ('guest_execution_verified', False),
                           ('working_tree_dirty', True), ('working_tree_dirty', 0)):
            data = completed(); data[key] = value
            with self.assertRaises(RuntimeError): validate_acceptance(data)


if __name__ == '__main__': unittest.main()
