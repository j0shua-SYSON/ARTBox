"""Check LLVM register-context restoration in signed Apple code and native Linux."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import zipfile

from environment import ROOT, environment
from sources import obtain
from ndk import obtain as obtain_ndk
from dynamic_bundle import prepare
from bionic_adapt import inventory

SOURCE = 'libunwind/src/UnwindRegistersRestore.S'
SOURCE_SHA = '7c66721db60ae08b776119e0475bb4efe6aeaec79dc0386c8cd35cb4aee268f1'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*command):
    return subprocess.check_output(list(map(str, command)))


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def adapt(data):
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA:
        raise RuntimeError('LLVM context source differs from the reviewed pin')
    before = b'  ldp    x18,x19, [x0, #0x090]\n'
    after = (b'  // ARTBox: preserve the live Apple platform register; restore adjacent x19.\n'
             b'  ldr    x19,     [x0, #0x098]\n')
    if data.count(before) != 1:
        raise RuntimeError('LLVM context adaptation is missing or ambiguous')
    return data.replace(before, after, 1)


def x18_writes(disassembly):
    writes = []
    for instruction in re.findall(r'^\s*[0-9a-f]+:\s+(.+)$', disassembly, re.M):
        words = instruction.split(None, 1)
        if len(words) != 2:
            continue
        op, operands = words
        registers = re.findall(r'\b[wx]\d+\b', operands.split('[')[0])
        if not registers:
            continue
        if op in ('stp', 'str', 'cmp', 'cmn', 'tst', 'ccmp', 'ccmn'):
            continue
        destinations = registers[:2] if op == 'ldp' else registers[:1]
        if any(r in ('x18', 'w18') for r in destinations):
            writes.append(instruction)
    return writes


def execute(runner, arguments, output, expected):
    result = subprocess.run([str(runner), *map(str, arguments)], capture_output=True,
                            encoding='utf-8', timeout=30)
    output.write_text(result.stdout + result.stderr, encoding='utf-8')
    result.check_returncode()
    observed = json.loads(result.stdout)
    if observed != {'checks': 4, 'failure_mask': expected,
                    'expected_rejection': bool(expected), 'cleanup': True}:
        raise RuntimeError('Unexpected register-context result')
    return observed


def build(args, output, artifacts):
    source = obtain('unwind-context')
    spec = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))['unwind-context']
    original = (source / SOURCE).read_bytes()
    changed = adapt(original)
    for invalid in (original + b'\n', changed):
        try:
            adapt(invalid)
        except RuntimeError:
            pass
        else:
            raise RuntimeError('Context adapter accepted source drift')
    adapted = output / 'UnwindRegistersRestore.S'
    adapted.write_bytes(changed)
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32': 'windows-x86_64', 'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64'}[sys.platform]
    tc = ndk / 'toolchains/llvm/prebuilt' / host
    suffix = '.exe' if os.name == 'nt' else ''
    tool = lambda name: tc / 'bin' / (name + suffix)
    flags = ['--target=aarch64-linux-android35', '-march=armv8-a', '-mbranch-protection=standard',
             '-D_LIBUNWIND_HIDE_SYMBOLS', '-I', source / 'libunwind/src']
    archive = tc / 'lib/clang/19/lib/linux/aarch64/libunwind.a'
    originals, equivalence = {}, {}
    for stem in ('UnwindRegistersRestore', 'UnwindRegistersSave'):
        obj = output / (stem + '.original.o')
        run(tool('clang'), *flags, '-c', source / ('libunwind/src/' + stem + '.S'), '-o', obj)
        member = output / (stem + '.ndk.o')
        member.write_bytes(run(tool('llvm-ar'), 'p', archive, stem + '.S.o'))
        sections = []
        for mode, path in [('source', obj), ('ndk', member)]:
            text = output / (stem + '.' + mode + '.text')
            copy = output / (stem + '.' + mode + '.copy.o')
            run(tool('llvm-objcopy'), '--dump-section', '.text=' + str(text), path, copy)
            sections.append(text.read_bytes())
        if not sections[0] or sections[0] != sections[1]:
            raise RuntimeError('Pinned LLVM context differs from the NDK machine code: ' + stem)
        originals[stem] = obj
        equivalence[stem] = {'text_bytes': len(sections[0]), 'text_sha256': hashlib.sha256(sections[0]).hexdigest(),
                             'ndk_member_sha256': digest(member), 'source_object_sha256': digest(obj)}
    restored = output / 'UnwindRegistersRestore.adapted.o'
    run(tool('clang'), *flags, '-c', adapted, '-o', restored)
    project = ['scripts/test_unwind_context.py', 'scripts/sources.py', 'scripts/ndk.py',
               'scripts/dynamic_bundle.py', 'scripts/bionic_adapt.py', 'scripts/environment.py',
               'tools/wrap_dynamic.py', 'fixtures/unwind-context/check.S', 'fixtures/unwind-context/linux.c',
               'tests/native_unwind_context.c', 'fixtures/bionic-dynamic/image.ld',
               'CMakeLists.txt', '.github/workflows/host-tests.yml', 'docs/m3-unwind-context.md',
               'third_party/sources.json', 'THIRD_PARTY.md', 'LICENSE']
    report = {'project_commit': run('git', 'rev-parse', 'HEAD').decode().strip(),
              'scope': 'LLVM register save/restore only; not ART startup or a complete unwinder',
              'sources': spec, 'project_sources': {p: digest(ROOT / p) for p in project},
              'equivalence': equivalence, 'adapted_source_sha256': digest(adapted),
              'adapted_object_sha256': digest(restored), 'source_drift_rejections': 2,
              'device_execution_verified': False, 'profiles': {}}
    for mode in ('original', 'adapted'):
        obj, elf = output / (mode + '.fixture.o'), output / (mode + '.so')
        extra = ['-DARTBOX_LINUX_UNWIND_NEGATIVE'] if mode == 'original' else []
        run(tool('clang'), *flags, *extra, '-Wall', '-Wextra', '-Werror', '-c',
            ROOT / 'fixtures/unwind-context/check.S', '-o', obj)
        restore = originals['UnwindRegistersRestore'] if mode == 'original' else restored
        run(tool('ld.lld'), '-shared', '-Bsymbolic', '--hash-style=both', '--build-id=none',
            '-z', 'defs', '-z', 'max-page-size=16384', '--pack-dyn-relocs=relr', '-soname', elf.name,
            '-T', ROOT / 'fixtures/bionic-dynamic/image.ld', obj, originals['UnwindRegistersSave'], restore, '-o', elf)
        disassembly = run(tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf).decode()
        (output / (mode + '.disassembly.txt')).write_text(disassembly, encoding='utf-8')
        boundary, writes = inventory(disassembly), x18_writes(disassembly)
        if not boundary['instruction_count'] or any(boundary[k] for k in
                ('svc', 'tpidr_mentions', 'unknown_instructions')):
            raise RuntimeError('Unexpected kernel/TLS/unknown instruction in context fixture')
        if len(writes) != (2 if mode == 'original' else 0):
            raise RuntimeError('Unexpected x18 writes in context fixture: ' + mode)
        imports = run(tool('llvm-nm'), '--undefined-only', '--format=posix', elf).strip()
        if imports:
            raise RuntimeError('Context fixture still imports external symbols')
        report['profiles'][mode] = {'elf_sha256': digest(elf), 'fixture_object_sha256': digest(obj),
                                     'inventory': boundary, 'x18_writes': writes}
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for f in spec['files']:
            z.write(source / f['path'], 'upstream/' + f['path'])
        z.write(adapted, 'adapted/' + SOURCE)
        for p in project:
            z.write(ROOT / p, 'artbox/' + p)
    report['source_bundle_sha256'] = digest(bundle)
    save(artifacts / 'm3-unwind-context.json', report)
    run(sys.executable, '-B', ROOT / 'tools/wrap_dynamic.py', output / 'adapted.so', output / 'pack')
    notice = source / spec['notice']
    report['frameworks'] = {}
    if sys.platform == 'darwin':
        if platform.machine().lower() not in ('arm64', 'aarch64'):
            raise RuntimeError('Context execution requires native ARM64 macOS')
        for target in ('macos', 'ios'):
            binary, details = prepare(output / 'pack', output / target, target, notice,
                                      spec['notice_sha256'], name='ARTBoxUnwindContext',
                                      notice_name='LLVM-LICENSE.txt')
            report['frameworks'][target] = details
            if target == 'macos':
                runner = Path(os.environ['ARTBOX_BUILD_DIR']) / 'host/artbox_native_unwind_context'
                report['native'] = execute(runner, [binary, output / 'adapted.so'], output / 'native.log', 0)
            save(artifacts / 'm3-unwind-context.json', report)
    save(artifacts / 'm3-unwind-context.json', report)
    print('Register-context source equivalence and both fixtures verified; ' +
          ('signed Mac checks pass, iOS 15 framework verified' if sys.platform == 'darwin' else 'native execution pending'))


def linux(args, output, artifacts):
    if sys.platform != 'linux' or platform.machine().lower() not in ('arm64', 'aarch64'):
        raise RuntimeError('Context reference execution requires native ARM64 Linux')
    evidence = args.evidence_root.resolve()
    record = json.loads((evidence / 'artifacts/m3-unwind-context.json').read_text(encoding='utf-8'))
    if record['project_commit'] != run('git', 'rev-parse', 'HEAD').decode().strip():
        raise RuntimeError('Context evidence belongs to a different revision')
    for path, expected in record['project_sources'].items():
        if digest(ROOT / path) != expected:
            raise RuntimeError('Context project input differs: ' + path)
    source = evidence / 'build/m3/unwind-context'
    if digest(source / 'corresponding-source.zip') != record['source_bundle_sha256']:
        raise RuntimeError('Context corresponding source changed')
    runner = output / 'context-linux'
    run(args.compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        ROOT / 'fixtures/unwind-context/linux.c', '-ldl', '-o', runner)
    results = {}
    for mode in ('original', 'adapted'):
        elf = source / (mode + '.so')
        if digest(elf) != record['profiles'][mode]['elf_sha256']:
            raise RuntimeError('Context ELF changed: ' + mode)
        results[mode] = execute(runner, [elf, mode], output / (mode + '.log'), int(mode == 'original'))
    if record['native'] != results['adapted']:
        raise RuntimeError('Signed Mac and Linux context checks disagree')
    save(artifacts / 'm3-unwind-context-linux.json', {'project_commit': record['project_commit'],
         'profiles': results, 'mac_report_sha256': digest(evidence / 'artifacts/m3-unwind-context.json')})
    print('Native Linux: original x18 corruption observed; adapted context checks pass')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--evidence-root', type=Path)
    parser.add_argument('--compiler', default='cc')
    args = parser.parse_args()
    os.environ.update(environment())
    output = args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm3/unwind-context'
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    output.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    (linux if args.evidence_root else build)(args, output, artifacts)


if __name__ == '__main__':
    main()
