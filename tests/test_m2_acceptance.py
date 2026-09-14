import sys
sys.dont_write_bytecode = True
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from m2_acceptance import evaluate


def completed():
    return dict(cases=146, futex_cases=19, file_cases=41, mapping_cases=43, vm_cases=35,
                timeout_cases=18, proc_cases=22, pthread_result=0, threads_reaped=6,
                tls_result=0, tls_modules=2, tls_threads=7, version_result=46,
                constructors=3, linked_images=4, absent_netd=1)


class Acceptance(unittest.TestCase):
    def test_complete_and_unexecuted_runs_keep_the_same_denominator(self):
        good, absent = evaluate(completed()), evaluate({})
        self.assertEqual(good['passed'], 328)
        self.assertTrue(good['success'])
        self.assertEqual((absent['total'], absent['passed'], absent['unexecuted']), (328, 0, 328))
        self.assertFalse(absent['success'])

    def test_failed_or_missing_workload_cannot_hide_behind_percentage(self):
        for change in ('failure', 'missing', 'boolean'):
            run = completed()
            if change == 'failure': run['pthread_result'] = -81
            elif change == 'missing': del run['threads_reaped']
            else: run['pthread_result'] = False
            result = evaluate(run)
            self.assertEqual((result['total'], result['passed']), (328, 327))
            self.assertGreater(result['percent'], 90)
            self.assertFalse(result['success'])

    def test_inflated_or_partial_numbered_counts_never_increase_score(self):
        for count in (145, 147, 1000000, -50):
            run = completed(); run['cases'] = count
            result = evaluate(run)
            self.assertEqual((result['total'], result['passed'], result['failed']), (328, 182, 146))
            self.assertFalse(result['success'])


if __name__ == '__main__': unittest.main()
