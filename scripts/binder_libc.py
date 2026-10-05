"""Build a native Linux oracle from the shared caller and original Bionic sources."""
# SPDX-License-Identifier: MIT
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from environment import ROOT

EXPECTED = dict(cases=50, path_control=-108, clock_control=-211,
                regex_cases=120, regex_newline_control=-1001, regex_capture_control=-2002)
SIGNED_EXPECTED = {('binder_' if key.startswith('regex_') else 'binder_libc_') + key: value
                   for key, value in EXPECTED.items()}
REGEX_SOURCES = ['libc/upstream-netbsd/lib/libc/regex/' + name
                 for name in ('regcomp.c', 'regexec.c', 'regfree.c', 'regerror.c')]
REGEX_FLAGS = ['-Wno-sign-compare', '-Wno-unused-parameter', '-DPOSIX_MISTAKE',
               '-include', 'netbsd-compat.h']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare_reference(output, source, tools, ndk, caller, regex_caller):
    output.mkdir(parents=True, exist_ok=True)
    pin = json.loads((ROOT / 'third_party/bionic/binder-libc.json').read_text(encoding='utf-8'))
    bionic = json.loads((ROOT / 'third_party/sources.json').read_text(encoding='utf-8'))['bionic']
    if pin['bionic_commit'] != bionic['commit'] or pin['files'][bionic['notice']] != bionic['notice_sha256']:
        raise RuntimeError('Binder libc inputs differ from the reviewed Bionic pin')
    selection = json.loads((ROOT / 'third_party/bionic/m2-objects.json').read_text(encoding='utf-8'))
    if (selection['source_commit'] != pin['bionic_commit'] or
            any(name not in selection['sources'] or
                selection['art_runtime_sources'].get(name) != pin['files'][name] or
                selection['source_flags'].get(name) != REGEX_FLAGS for name in REGEX_SOURCES)):
        raise RuntimeError('Native and reference regex source selection or Soong flags differ')
    for name, expected in pin['files'].items():
        if digest(source / name) != expected:
            raise RuntimeError('Binder libc reference source changed: ' + name)
    suffix = '.exe' if (tools / 'clang.exe').is_file() else ''
    tool = lambda name: tools / (name + suffix)
    commands = []

    def run(words, label):
        command = list(map(str, words))
        commands.append([word.replace(str(ROOT), '${ARTBOX_ROOT}') for word in command])
        result = subprocess.run(command, capture_output=True, timeout=120)
        (output / (label + '.log')).write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Binder libc reference build failed: ' + label)

    flags = ['--target=aarch64-linux-android35', '-O2', '-fPIC', '-fno-builtin', '-fno-stack-protector',
             '-ffixed-x18', '-ffixed-x27', '-ffixed-x28', '-march=armv8-a', '-mno-outline-atomics',
             '-ffile-prefix-map=' + str(ROOT) + '=.', '-Wall', '-Wextra', '-Werror']
    objects = []
    for name in ['libc/bionic/time.cpp', 'libc/upstream-openbsd/lib/libc/gen/fnmatch.c', *REGEX_SOURCES]:
        obj = output / (Path(name).name + '.o')
        cpp = name.endswith('.cpp')
        language = ['-std=c++20', '-fno-exceptions', '-fno-rtti'] if cpp else ['-std=gnu99']
        selected = [*REGEX_FLAGS, '-I', source / 'libc/upstream-netbsd/android/include',
                    '-I', source / 'libc/upstream-netbsd/lib/libc/include'] if name in REGEX_SOURCES else []
        run([tool('clang++' if cpp else 'clang'), *flags, *language, *selected,
             '-c', source / name, '-o', obj], obj.name)
        objects.append(obj)
    # Static NDK libc supplies the oracle's ordinary Linux syscall/TLS runtime.
    # This executable is never packaged as Apple code or executed on another ISA.
    binary = output / 'binder-libc-linux'
    run([tool('clang'), *flags, '-std=c11', '-I', ROOT / 'platform/include', '-static',
         '-Wl,-z,max-page-size=65536', '-Wl,-Map,' + str(output / 'reference.map'),
         ROOT / 'fixtures/binder-libc/linux.c', caller, regex_caller, *objects, '-o', binary], 'link')
    files = {name: source / name for name in pin['files']}
    project = ['LICENSE', 'THIRD_PARTY.md', 'fixtures/binder-libc/check.c', 'fixtures/binder-libc/regex.c',
               'fixtures/binder-libc/linux.c',
               'platform/include/artbox/binder_libc_result.h', 'scripts/binder_libc.py',
               'scripts/test_binder_libc.py', 'scripts/environment.py', 'third_party/bionic/binder-libc.json',
               'third_party/bionic/m2-objects.json']
    files.update({'artbox/' + name: ROOT / name for name in project})
    notices = {'NDK-NOTICE.txt': ndk / 'NOTICE', 'NDK-TOOLCHAIN-NOTICE.txt': tools.parent / 'NOTICE'}
    expected_notice = json.loads((ROOT / 'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
    if digest(notices['NDK-TOOLCHAIN-NOTICE.txt']) != expected_notice:
        raise RuntimeError('Binder libc oracle toolchain license changed')
    files.update(notices)
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, path.read_bytes())
    record = dict(scope='Native Linux ARM64 static oracle; shared caller plus original Bionic functions',
                  pin=pin, expected=EXPECTED, commands=commands,
                  caller_sha256=digest(caller), regex_caller_sha256=digest(regex_caller),
                  executable_sha256=digest(binary),
                  objects={p.name: digest(p) for p in objects}, source_bundle_sha256=digest(bundle),
                  source_files={name: digest(path) for name, path in files.items()}, execution_verified=False)
    (output / 'build.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    return dict(source_sha256=digest(ROOT / 'fixtures/binder-libc/check.c'), object_sha256=digest(caller),
                regex_source_sha256=digest(ROOT / 'fixtures/binder-libc/regex.c'),
                regex_object_sha256=digest(regex_caller),
                expected=EXPECTED, reference_manifest_sha256=digest(output / 'build.json'))
