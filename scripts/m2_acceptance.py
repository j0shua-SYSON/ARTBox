"""Score fixed M2 expectations without dropping failed or unexecuted groups."""
import hashlib
import json
from environment import ROOT

NUMBERED = {'allocator_devices': 'cases', 'futex': 'futex_cases', 'regular_files': 'file_cases',
            'file_mappings': 'mapping_cases', 'anonymous_memory': 'vm_cases',
            'pthread_timeouts': 'timeout_cases', 'proc_commandline': 'proc_cases'}
WORKLOADS = {'pthread_workload': {'pthread_result': 0, 'threads_reaped': 6},
             'elf_tls': {'tls_result': 0, 'tls_modules': 2, 'tls_threads': 7},
             'symbol_versions': {'version_result': 46},
             'startup_constructors': {'constructors': 3, 'linked_images': 4, 'absent_netd': 1}}


def manifest():
    data = (ROOT / 'fixtures/bionic-startup/acceptance.json').read_bytes()
    contract = json.loads(data)
    entries = contract['cases']
    if contract['version'] != 1 or set(e['id'] for e in entries) != set(NUMBERED) | set(WORKLOADS) or \
            len(entries) != len(NUMBERED) + len(WORKLOADS) or \
            any(type(e['count']) is not int or e['count'] < 1 or type(e['mandatory']) is not bool for e in entries) or \
            sum(e['count'] for e in entries) != contract['total'] or contract['total'] != 328:
        raise ValueError('M2 version-1 denominator changed or is malformed')
    # Canonical JSON keeps the identity stable across checkout line endings.
    canonical = json.dumps(contract, sort_keys=True, separators=(',', ':')).encode('utf-8')
    return contract, hashlib.sha256(canonical).hexdigest()


def evaluate(observed):
    contract, sha = manifest()
    groups = []
    for entry in contract['cases']:
        name, count = entry['id'], entry['count']
        expected = {NUMBERED[name]: count} if name in NUMBERED else WORKLOADS[name]
        missing = any(k not in observed for k in expected)
        passed = not missing and all(type(observed[k]) is int and observed[k] == v for k, v in expected.items())
        groups.append({**entry, 'passed': count if passed else 0, 'failed': count if not passed and not missing else 0,
                       'unexecuted': count if missing else 0})
    passed = sum(g['passed'] for g in groups)
    percentage = 100 * passed / contract['total']
    return {'manifest_sha256': sha, 'total': contract['total'], 'passed': passed,
            'failed': sum(g['failed'] for g in groups), 'unexecuted': sum(g['unexecuted'] for g in groups),
            'percent': percentage, 'success': percentage >= contract['minimum_percent'] and
            all(g['passed'] == g['count'] for g in groups if g['mandatory']), 'groups': groups}
