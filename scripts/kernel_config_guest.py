"""Shared parser observations and producer identity for Linux and signed guests."""
# SPDX-License-Identifier: MIT
import hashlib
import json
from pathlib import Path
from environment import ROOT

EXPECTED = dict(cases=89, comments_control=-104, relaxed_control=-105)
OBJECTS = {'check.o', 'KernelConfigParser.o', 'regex.cpp.o', 'reference-main.o'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def verify(path, expected):
    path = Path(path)
    if not path.is_file() or digest(path) != expected:
        raise ValueError('Kernel-config input differs from its producer: ' + str(path))
    return path


def verify_contract(exit_code, result):
    if (type(exit_code) is not int or exit_code != 0 or result != EXPECTED or
            any(type(value) is not int for value in result.values())):
        raise ValueError('Parser differs from the shared positive and negative contract')


def verify_guest(exit_code, result):
    verify_contract(exit_code, result.get('kernel_config'))
    exact = dict(tls_modules=1, linked_images=5, registered_vms=0,
                 heap_binding_verified=True, sigchain_cases=22, sigchain_mutation=-1005,
                 runtime_started=False, dex_executed=False, threads_reaped=0, cleanup=True)
    if any(type(result.get(k)) is not type(v) or result.get(k) != v for k,v in exact.items()):
        raise ValueError('Incomplete signed parser ownership or cleanup')
    for key in ('constructors', 'kernel_config_check_ns', 'load_relocate_ns', 'bootstrap_ns'):
        if type(result.get(key)) is not int or result[key] <= 0:
            raise ValueError('Missing parser execution observation: ' + key)


def verify_reference(reference, build, build_sha256):
    if (reference.get('native_linux_execution_verified') is not True or
            reference.get('project_commit') != build['project_commit'] or
            reference.get('build_result_sha256') != build_sha256 or
            reference.get('objects') != build['objects'] or
            reference.get('reference_sha256') != build['reference_sha256'] or
            reference.get('source_bundle_sha256') != build['source_bundle_sha256']):
        raise ValueError('Signed parser requires execution of the identical Linux caller and dependencies')
    verify_contract(reference.get('exit'), reference.get('native'))


def verify_build(directory, revision):
    directory = Path(directory)
    build = read(directory / 'result.json')
    if (build['project_commit'] != revision or build['working_tree_dirty'] is not False or
            set(build['objects']) != OBJECTS or
            build['regex_runtime'] != read(ROOT / 'third_party/binder/regex-runtime.json')):
        raise ValueError('Require the current clean Android parser and pinned regex object')
    for name, expected in build['project_sources'].items(): verify(ROOT / name, expected)
    for name, expected in build['objects'].items(): verify(directory / name, expected)
    if build['objects']['regex.cpp.o'] != build['regex_runtime']['member_sha256']:
        raise ValueError('Parser regex object differs from reviewed archive member')
    verify(directory / 'linux-reference', build['reference_sha256'])
    verify(directory / 'corresponding-source.zip', build['source_bundle_sha256'])
    return build
