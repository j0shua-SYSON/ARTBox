"""Acceptance observations for the original MessageQueue JNI running in ART."""
# SPDX-License-Identifier: MIT
import json


def verify_records(text):
    prefix = 'ARTBox framework queue: '
    records = [json.loads(line[len(prefix):]) for line in text.splitlines() if line.startswith(prefix)]
    expected = [('wake',0,24,0,1), ('omit-wake',-112,15,112,0)]
    if len(records) != len(expected): raise ValueError('Missing or duplicate ART queue observation')
    for record,(mode,status,checks,failure,wakes) in zip(records,expected):
        values = dict(status=status, checks=checks, failure=failure, wake_calls=wakes, attachments=1,
                      disposed=1, native_queue_released=1, looper_released=1)
        if (record.get('mode') != mode or
                any(type(record.get(key)) is not int or record[key] != value for key,value in values.items()) or
                type(record.get('poll_ns')) is not int):
            raise ValueError('Incomplete ART queue wake/control/ownership contract')
        duration = record['poll_ns']
        if not (0 < duration < 2000000000 if mode == 'wake' else duration >= 2500000000):
            raise ValueError('ART queue did not observe a wake and an omitted-wake timeout')
    return records
