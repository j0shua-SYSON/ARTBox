"""Failure reports must distinguish entering ART from completing its contract."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from test_icu_guest import runtime_progress


class RuntimeProgress(unittest.TestCase):
    def test_startup_abort_preserves_attempt_without_claiming_a_vm(self):
        result = runtime_progress(b'ARTBox: entering signed ART JNI_CreateJavaVM\n'
                                  b'F thread.cc:1298 pthread_getattr_np failed\n')
        self.assertTrue(result['runtime_invocation_attempted'])
        self.assertFalse(result['runtime_started'])
        self.assertFalse(result['dex_executed'])
        self.assertFalse(result['lifecycle_verified'])

    def test_missing_class_preserves_startup_without_claiming_dex_or_shutdown(self):
        result = runtime_progress(b'ARTBox: entering signed ART JNI_CreateJavaVM\n'
            b'ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache\n'
            b'signed ART runtime result: 3\n')
        self.assertTrue(result['runtime_started'])
        self.assertFalse(result['dex_executed'])
        self.assertFalse(result['lifecycle_verified'])

    def test_truncated_or_embedded_messages_are_not_phase_observations(self):
        result = runtime_progress(b'diagnostic: ARTBox: signed ART lifecycle checks passed\n'
            b'ARTBox: signed ART method returned the expected str\xff')
        self.assertFalse(any(result.values()))


if __name__ == '__main__': unittest.main()
