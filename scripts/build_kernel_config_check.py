"""Build one shared NDK parser caller for signed packaging and native Linux comparison."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import zipfile

from environment import ROOT, environment
from sources import obtain_files
from ndk import REVISION, obtain as obtain_ndk
from build_binder import ndk_notices, verify_object_code


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    args = parser.parse_args()
    os.environ.update(environment())
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/kernel-config').resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = output / 'result.json'
    if report.exists(): report.unlink()
    commands = []

    def run(words, label, timeout=120):
        command = list(map(str, words)); commands.append(command)
        process = subprocess.run(command, capture_output=True, timeout=timeout)
        (output / (label + '.stdout')).write_bytes(process.stdout)
        (output / (label + '.stderr')).write_bytes(process.stderr)
        if process.returncode:
            sys.stderr.buffer.write(process.stderr)
            raise RuntimeError('Kernel-config build failed: ' + label)
        return process.stdout

    ndk = obtain_ndk(args.ndk_root)
    host = {'win32':'windows-x86_64', 'darwin':'darwin-x86_64', 'linux':'linux-x86_64'}[sys.platform]
    toolchain = ndk / 'toolchains/llvm/prebuilt' / host
    suffix = '.exe' if os.name == 'nt' else ''
    tool = lambda name: toolchain / 'bin' / (name + suffix)
    selection = read(ROOT / 'third_party/binder/vintf-sources.json')
    catalog = {name: selection[name] for name in ('vintf', 'vintf-utils-headers')}
    sources = {name: obtain_files('binder-' + name, spec, Path(os.environ['ARTBOX_CACHE_DIR']))
               for name, spec in catalog.items()}
    regex = read(ROOT / 'third_party/binder/regex-runtime.json')
    if regex['ndk_revision'] != REVISION or digest(toolchain / regex['notice']) != regex['notice_sha256']:
        raise RuntimeError('libc++ regex dependency differs from its reviewed NDK/license pin')
    archive = toolchain / regex['archive']
    members = run([tool('llvm-ar'), 't', archive], 'archive-members').decode().splitlines()
    if members.count(regex['member']) != 1:
        raise RuntimeError('Require exactly one reviewed libc++ regex member')
    member = output / regex['member']
    member.write_bytes(run([tool('llvm-ar'), 'p', archive, regex['member']], 'extract-regex'))
    if digest(member) != regex['member_sha256']:
        raise RuntimeError('Original libc++ regex object differs from its pin')
    notices = ndk_notices(ndk, toolchain)
    flags = ['--target=aarch64-linux-android35', '-std=c++20', '-O1', '-DNDEBUG', '-fPIC',
             '-fno-exceptions', '-fno-rtti', '-march=armv8-a', '-mno-outline-atomics',
             '-ffixed-x18', '-ffixed-x27', '-ffixed-x28', '-Wall', '-Wextra', '-Werror',
             '-ffile-prefix-map=' + str(ROOT) + '=.', '-fdebug-prefix-map=' + str(ROOT) + '=.']
    includes = [sources['vintf'] / 'include',
                sources['vintf-utils-headers'] / 'libutils/binder/include']
    include_flags = [word for path in includes for word in ('-I', str(path))]
    include_flags += ['-iquote', str(sources['vintf'] / 'include/vintf')]
    units = {'check.o': ROOT / 'fixtures/kernel-config/check.cpp',
             'KernelConfigParser.o': sources['vintf'] / 'KernelConfigParser.cpp',
             'reference-main.o': ROOT / 'tests/native_kernel_config.cpp'}
    started = time.monotonic()
    objects, boundaries = {member.name:digest(member)}, {}
    boundaries[member.name] = verify_object_code(member, run(
        [tool('llvm-objdump'), '-d', '--disassemble-zeroes', '--no-show-raw-insn', member],
        member.name + '-code').decode())
    for name, source in units.items():
        obj = output / name
        run([tool('clang++'), *flags, *include_flags, '-c', source, '-o', obj], name)
        objects[name] = digest(obj)
        boundaries[name] = verify_object_code(obj, run(
            [tool('llvm-objdump'), '-d', '--disassemble-zeroes', '--no-show-raw-insn', obj],
            name + '-code').decode())
    binary = output / 'linux-reference'
    # This links the same caller and parser objects used by the signed payload.
    # The native Linux executable uses the original NDK's complete libc/STL.
    run([tool('clang++'), '--target=aarch64-linux-android35', '-static', '-Wl,--build-id=none',
         '-Wl,--why-extract=' + str(output / 'reference-extracted.tsv'),
         output / 'check.o', output / 'KernelConfigParser.o', output / 'reference-main.o',
         '-o', binary], 'reference-link')
    extracted = (output / 'reference-extracted.tsv').read_text()
    if extracted.count('libc++_static.a(' + regex['member'] + ')') != 1:
        raise RuntimeError('Linux reference did not select the reviewed libc++ regex member')
    files = {}
    for name, spec in catalog.items():
        for row in spec['files']:
            path = sources[name] / row['path']
            if digest(path) != row['sha256']: raise RuntimeError('Parser input changed: ' + row['path'])
            files['upstream/' + name + '/' + row['path']] = path
        notice = sources[name] / spec['notice']
        if digest(notice) != spec['notice_sha256']: raise RuntimeError('Parser license changed: ' + name)
        notices[name + '.txt'] = notice
    for name, path in notices.items(): files['notices/' + name] = path
    project = ['LICENSE', 'THIRD_PARTY.md', 'fixtures/kernel-config/check.cpp',
               'tests/native_kernel_config.cpp', 'scripts/build_kernel_config_check.py',
               'scripts/environment.py', 'scripts/sources.py', 'scripts/ndk.py',
               'scripts/build_binder.py', 'scripts/binder_aidl.py', 'scripts/bionic_adapt.py',
               'scripts/art_native_tls.py', 'scripts/tls_adapt.py',
               'third_party/binder/regex-runtime.json', 'third_party/binder/vintf-sources.json',
               'third_party/binder/aidl-tools.json', 'third_party/bionic/builtins.json']
    for name in project: files['artbox/' + name] = ROOT / name
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as target:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            target.writestr(entry, path.read_bytes())
    result = dict(project_commit=run(['git','rev-parse','HEAD'],'revision').decode().strip(),
                  working_tree_dirty=bool(run(['git','status','--porcelain'],'worktree').strip()),
                  scope='Shared Android caller and original parser; native Linux execution is a separate gate',
                  sources=catalog, regex_runtime=regex, objects=objects, boundaries=boundaries,
                  unit_source_sha256={name:digest(source) for name,source in units.items()},
                  ndk_archive_sha256=digest(archive), reference_sha256=digest(binary),
                  compiler_version=run([tool('clang++'),'--version'],'compiler').decode().strip(),
                  commands=commands, project_sources={name:digest(ROOT / name) for name in project},
                  notices={name:digest(path) for name,path in notices.items()},
                  source_bundle_sha256=digest(bundle), build_seconds=time.monotonic()-started,
                  native_linux_execution_verified=False, guest_execution_verified=False)
    report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Original parser and shared caller built; Linux reference and signed execution remain required')


if __name__ == '__main__': main()
