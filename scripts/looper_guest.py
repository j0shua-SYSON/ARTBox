"""Exact shared Looper acceptance, including cleanup of deliberate failures."""
# SPDX-License-Identifier: MIT
EXPECTED = {
    'normal': dict(status=0, cases=43, failure=0, wake_threads=1, message_calls=3,
                   fd_callbacks=1, timer_callbacks=1),
    'retain-callback': dict(status=-205, cases=17, failure=205, wake_threads=1,
                            message_calls=0, fd_callbacks=1, timer_callbacks=0),
    'omit-wake': dict(status=-109, cases=10, failure=109, wake_threads=0,
                      message_calls=0, fd_callbacks=0, timer_callbacks=0),
}


def verify_result(name, exit_code, result):
    if name not in EXPECTED:
        raise ValueError('Unknown Looper control')
    observed = result.get('looper')
    if (exit_code != (0 if name == 'normal' else 1) or observed != EXPECTED[name] or
            any(type(value) is not int for value in observed.values())):
        raise ValueError('Signed Looper differs from the shared native contract: ' + name)
    exact = dict(tls_modules=1, linked_images=5, registered_vms=0,
                 heap_binding_verified=True, sigchain_cases=22, sigchain_mutation=-1005,
                 runtime_started=False, dex_executed=False, threads_reaped=1, cleanup=True)
    if any(type(result.get(key)) is not type(value) or result.get(key) != value for key, value in exact.items()):
        raise ValueError('Incomplete signed Looper ownership or cleanup')
    for key in ('constructors', 'looper_check_ns', 'load_relocate_ns', 'bootstrap_ns'):
        if type(result.get(key)) is not int or result[key] <= 0:
            raise ValueError('Missing signed Looper execution observation: ' + key)
    return result
