"""Verify and stage the tested M2 device libraries as iOS build inputs."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
sys.dont_write_bytecode = True
from environment import ROOT
sys.path.insert(0, str(ROOT / 'tools'))
from wrap_dynamic import pack_layout, verify_macho
from m2_acceptance import evaluate

IMAGES = [('libc', 'libc.so', 'ARTBoxBionic'), ('client', 'libstartup_client.so', 'ARTBoxStartupClient'),
          ('versions', 'libartbox_versions.so', 'ARTBoxVersions'), ('tls', 'libartbox_tls.so', 'ARTBoxTLS')]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(evidence, output, revision):
    evidence, output = Path(evidence).resolve(), Path(output).resolve()
    report_path = evidence / 'artifacts/m2-bionic-startup.json'
    report = json.loads(report_path.read_text(encoding='utf-8'))
    if report.get('project_commit') != revision:
        raise RuntimeError('M2 inputs must come from this exact project revision')
    expected = {'cases': 146, 'futex_cases': 19, 'file_cases': 41, 'mapping_cases': 43,
                'pthread_result': 0, 'threads_reaped': 6, 'tls_modules': 2, 'tls_threads': 7,
                'tls_result': 0, 'linked_images': 4, 'version_result': 46}
    expected.update(vm_cases=35, timeout_cases=18, art_libc_cases=30)
    for mode in ('native', 'sampled_native'):
        if any(report[mode].get(k) != v for k, v in expected.items()):
            raise RuntimeError('M2 source artifact did not pass the required native suites')
        scored = evaluate(report[mode])
        if not scored['success'] or report['acceptance'][mode] != scored:
            raise RuntimeError('M2 source artifact lacks complete acceptance for the fixed denominator')
    source = evidence / 'build/m2/bionic-startup'
    (output / 'ELF').mkdir(parents=True, exist_ok=True)
    staged = {'project_commit': revision, 'source_report_sha256': digest(report_path), 'images': {},
              'native': report['native'], 'sampled_native': report['sampled_native'], 'tls': report['tls'],
              'acceptance': report['acceptance']}
    for key, elf_name, framework_name in IMAGES:
        details = report['images'][key]
        elf = source / elf_name
        if digest(elf) != details['elf_sha256']: raise RuntimeError('ELF hash mismatch: ' + key)
        layout = pack_layout(elf.read_bytes())[2]
        if layout != details['layout']: raise RuntimeError('ELF layout mismatch: ' + key)
        framework = source / key / 'ios' / (framework_name + '.framework')
        device = details['frameworks']['ios']
        if verify_macho((framework / framework_name).read_bytes(), layout) != device['layout']:
            raise RuntimeError('Signed iOS bytes or virtual layout mismatch: ' + key)
        for name, sha in device['notices'].items():
            if '/' in name or '\\' in name or Path(name).name != name or digest(framework / name) != sha:
                raise RuntimeError('Incomplete or changed third-party notice')
        shutil.copytree(framework, output / framework.name, dirs_exist_ok=True)
        shutil.copyfile(elf, output / 'ELF' / elf_name)
        staged['images'][key] = {'framework': framework_name, 'elf': elf_name, **details}
    (output / 'ELF/manifest.json').write_text(json.dumps(staged, indent=2) + '\n', encoding='utf-8')
    return staged
