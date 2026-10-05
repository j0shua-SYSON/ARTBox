"""Stage the exact signed Binder roles only after complete native acceptance."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import hashlib
import json
from pathlib import Path
import shutil
from environment import ROOT
sys.path.insert(0, str(ROOT / 'tools'))
from wrap_dynamic import pack_layout, verify_macho

ELFS = ('libc.so', 'libart.so', 'libm.so', 'libdl.so', 'libdl_android.so',
        'libartbox_servicemanager.so')
CASES = {'access': 20, 'wrong-uid': -109, 'wrong-pid': -107, 'roles': 0}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_acceptance(report):
    try:
        require(report['guest_execution_verified'] is True and report['working_tree_dirty'] is False,
                'Service requires successful execution from clean inputs')
        require(set(report['executions']) == set(CASES), 'Service requires all policy controls and native roles')
        for label, access in CASES.items():
            execution = report['executions'][label]
            require(not execution.get('timeout') and type(execution['exit']) is int and execution['exit'] == 0,
                    'Service process failed: ' + label)
            native = execution['native']
            roles = label == 'roles'
            exact = dict(access_cases=access, policy_cases=5, provider=32 if roles else 0,
                         client=32 if roles else 0, roles=3 if roles else 1,
                         independent_images=18 if roles else 6, manager_retained=roles,
                         finite_roles_cleaned=True, passed=True)
            require(all(type(native[k]) is type(v) and native[k] == v for k, v in exact.items()),
                    'Service acceptance is incomplete: ' + label)
            require(all(type(native[k]) is int and native[k] > 0 for k in ('elapsed_ns', 'peak_rss_bytes')),
                    'Service measurements are missing: ' + label)
    except (KeyError, TypeError) as error:
        raise RuntimeError('Incomplete service acceptance report') from error


def prepare(evidence, output, revision):
    evidence, output = Path(evidence).resolve(), Path(output).resolve()
    report_path = evidence / 'artifacts/m4-service-guest.json'
    report = json.loads(report_path.read_bytes())
    validate_acceptance(report)
    require(report['project_commit'] == revision, 'Service evidence must match this revision')
    for name, expected in report['project_sources'].items():
        require(digest(ROOT / name) == expected, 'Service project input changed: ' + name)
    base = evidence / 'build/m4/service-guest'
    require(digest(base / 'corresponding-source.zip') == report['source_bundle_sha256'],
            'Service corresponding source changed')
    for label in CASES:
        execution = report['executions'][label]
        for stream in ('stdout', 'stderr'):
            path = base / (label + '.' + stream)
            require(digest(path) == execution[stream + '_sha256'], 'Service retained output changed')
        require(json.loads((base / (label + '.stdout')).read_bytes()) == execution['native'],
                'Service result differs from retained process output')
    require(set(report['elf_sha256']) == set(ELFS), 'Service ELF set changed')
    require(set(report['frameworks']) == {f'{target}-{role}-{index}' for target in ('macos', 'ios')
                                        for role in range(3) for index in range(6)},
            'Service signed role set changed')
    images = []
    for index, elf in enumerate(ELFS):
        source = base / 'ELF' / elf
        require(digest(source) == report['elf_sha256'][elf], 'Service ELF bytes changed: ' + elf)
        layout = pack_layout(source.read_bytes())[2]
        for role in range(3):
            name = f'ARTBoxServiceR{role}M{index}'
            signed = {}
            for target in ('macos', 'ios'):
                bundle = base / target / name / (name + '.framework')
                proof = report['frameworks'][f'{target}-{role}-{index}']
                require(proof['signature_verified'] is True and proof['entitlements'] == {},
                        'Service frameworks require verified signatures and empty entitlements')
                require(verify_macho((bundle / name).read_bytes(), layout) == proof['layout'],
                        'Service signed code or layout changed: ' + name)
                for notice, expected in proof['notices'].items():
                    require(Path(notice).name == notice and '/' not in notice and '\\' not in notice and
                            digest(bundle / notice) == expected, 'Service notice changed')
                signed[target] = proof
            images.append(dict(role=role, index=index, elf=elf, framework=name,
                               elf_sha256=report['elf_sha256'][elf], layout=layout, frameworks=signed))
    images.sort(key=lambda row: (row['role'], row['index']))
    require(not output.exists() or not any(output.iterdir()), 'Service staging directory must be empty')
    (output / 'Resources/ELF').mkdir(parents=True, exist_ok=True)
    for elf in ELFS:
        shutil.copyfile(base / 'ELF' / elf, output / 'Resources/ELF' / elf)
    for item in images:
        name = item['framework']
        shutil.copytree(base / 'ios' / name / (name + '.framework'), output / (name + '.framework'))
    staged = dict(project_commit=revision, images=images, acceptance=report,
                  producer_report_sha256=digest(report_path), device_execution_verified=False)
    (output / 'Resources/manifest.json').write_text(json.dumps(staged, indent=2) + '\n', encoding='utf-8')
    return staged


def verify_embedded(app, staged):
    app = Path(app)
    resources = app / 'ARTBoxM4'
    require(json.loads((resources / 'manifest.json').read_bytes()) == staged, 'Embedded service manifest changed')
    for item in staged['images']:
        name = item['framework']
        bundle = app / 'Frameworks' / (name + '.framework')
        require(digest(resources / 'ELF' / item['elf']) == item['elf_sha256'], 'Embedded service ELF changed')
        require(verify_macho((bundle / name).read_bytes(), item['layout']) == item['frameworks']['ios']['layout'],
                'Embedded signed service image changed')
        for notice, expected in item['frameworks']['ios']['notices'].items():
            require(digest(bundle / notice) == expected, 'Embedded service notice changed')
