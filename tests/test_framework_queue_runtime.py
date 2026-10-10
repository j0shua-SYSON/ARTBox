"""Reject missing wake, cleanup or negative-control evidence from the ART queue test."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import copy
import json
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from framework_queue import verify_records


class QueueRuntimeEvidence(unittest.TestCase):
    def records(self):
        shared = dict(attachments=1, disposed=1, native_queue_released=1, looper_released=1)
        return [dict(shared, mode='wake', status=0, checks=24, failure=0, wake_calls=1, poll_ns=1000000),
                dict(shared, mode='omit-wake', status=-112, checks=15, failure=112, wake_calls=0, poll_ns=3000000000)]

    def encode(self, records):
        return '\n'.join('ARTBox framework queue: '+json.dumps(record) for record in records)

    def test_wake_timeout_and_both_owners_are_required(self):
        records = self.records()
        self.assertEqual(verify_records(self.encode(records)), records)
        mutations = [(0,'status',-112),(0,'wake_calls',0),(0,'poll_ns',2500000000),
                     (0,'checks',23),(0,'attachments',True),(0,'disposed',0),
                     (0,'native_queue_released',0),(1,'looper_released',0),
                     (1,'status',0),(1,'failure',0),(1,'poll_ns',1000000)]
        for index,key,value in mutations:
            changed = copy.deepcopy(records); changed[index][key] = value
            with self.subTest(key=key,index=index), self.assertRaises(ValueError):
                verify_records(self.encode(changed))

    def test_missing_duplicate_and_reordered_records_are_rejected(self):
        records = self.records()
        for changed in ([],records[:1],records+records,records[::-1]):
            with self.subTest(count=len(changed)), self.assertRaises(ValueError):
                verify_records(self.encode(changed))


if __name__ == '__main__': unittest.main()
