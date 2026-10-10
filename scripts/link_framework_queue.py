"""Link original MessageQueue JNI and real libutils into ordinary signed Apple code."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile
from environment import ROOT, environment
from build_art_classlib import digest, save
from build_binder import verify_object_architecture
from dynamic_bundle import prepare
from icu_guest_link import check_code
from ndk import obtain as obtain_ndk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('queue', 'looper', 'guest', 'dependency', 'icu'):
        parser.add_argument('--'+name+'-dir', required=True, type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--ndk-root', type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin': parser.error('Apple framework signing requires macOS')
    os.environ.update(environment())
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR'])/'m5/framework-queue-link').resolve()
    output.mkdir(parents=True, exist_ok=True)
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    artifacts.mkdir(parents=True, exist_ok=True)
    read = lambda p:json.loads(p.read_bytes())
    commands = []
    def run(words, label):
        command = list(map(str, words)); commands.append(command)
        process = subprocess.run(command, capture_output=True, timeout=120)
        (output/(label+'.stdout')).write_bytes(process.stdout)
        (output/(label+'.stderr')).write_bytes(process.stderr)
        if process.returncode:
            sys.stderr.buffer.write(process.stderr)
            raise RuntimeError('Framework queue link failed: '+label)
        return process.stdout.decode('utf-8')
    def verify(path, expected):
        if not path.is_file() or digest(path) != expected:
            raise RuntimeError('Framework queue input differs from its producer: '+str(path))
        return path

    revision = run(['git','rev-parse','HEAD'], 'revision').strip()
    if run(['git','status','--porcelain'], 'worktree').strip():
        raise RuntimeError('Framework queue requires a clean producer revision')
    result = read(args.queue_dir/'result.json')
    if not re.fullmatch(r'attempt-[A-Za-z0-9_-]+', result['attempt']):
        raise RuntimeError('Invalid queue producer attempt')
    queue, build = args.queue_dir/result['attempt'], result['report']
    looper = read(args.looper_dir/'result.json')
    art = read(artifacts/'m3-art-guest-link.json')
    icu = read(artifacts/'m3-icu-guest-link.json')
    for record in (build, looper, art, icu):
        if record['project_commit'] != revision or record['working_tree_dirty']:
            raise RuntimeError('Require the same clean revision for all framework inputs')
        for name, expected in record['project_sources'].items(): verify(ROOT/name, expected)
    if (build['runtime_executed'] or build['compile_reference_packaged'] or looper['profile'] != 'android' or
            art['input_revision'] != revision or icu['input_revision'] != revision or
            build['profile_sha256'] != digest(ROOT/'third_party/framework/queue.json') or
            build['sources'] != read(ROOT/'third_party/framework/sources.json')):
        raise RuntimeError('Framework input profile changed')
    if set(build['native_objects']) != {'MessageQueue.o', 'check.o', 'runtime.o'}:
        raise RuntimeError('Queue JNI/caller object selection changed')
    objects = [verify(queue/name, item['sha256']) for name,item in build['native_objects'].items()]
    graph = read(ROOT/'third_party/binder/native-libraries.json')['libraries']
    names = [name.replace('/', '__')+'.o' for library in ('libutils_binder', 'libutils_looper') for name in graph[library]['units']]
    if len(names) != 10 or set(looper['objects']) != set(names+['check.cpp.o']):
        raise RuntimeError('Original Looper object selection changed')
    objects += [verify(args.looper_dir/name, looper['objects'][name]) for name in names]
    for obj in objects: verify_object_architecture(obj)
    deps = args.dependency_dir
    base = {
        'libc.so':deps/'build/m2/bionic-startup/libc.so',
        'libart.so':args.guest_dir/'libart.so',
        'libm.so':deps/'build/m3/art-math/libm.so',
        'libdl.so':deps/'build/m3/guest-loader/libdl.so',
        'libnativehelper.so':args.icu_dir/'libnativehelper.so'}
    expected = {**art['dependencies'], 'libart.so':art['elf_sha256'],
                'libnativehelper.so':icu['libraries']['libnativehelper.so']['elf_sha256']}
    for name,path in base.items(): verify(path, expected[name])
    dex = verify(queue/'framework-queue.dex', build['dex']['sha256'])
    if build['dex']['classes'] != 6 or build['dex']['version'] != '039':
        raise RuntimeError('Unexpected managed queue payload')
    (output/dex.name).write_bytes(dex.read_bytes())
    ndk = obtain_ndk(args.ndk_root)
    tc = ndk/'toolchains/llvm/prebuilt/darwin-x86_64'
    tool = lambda name:tc/'bin'/name
    crt = tc/'sysroot/usr/lib/aarch64-linux-android/35'
    linker = output/'image.ld'
    script = (ROOT/'fixtures/bionic-dynamic/image.ld').read_text(encoding='utf-8')
    script = script.replace('KEEP(*(.init_array .init_array.*))',
                            'KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))')
    script = script.replace('*(COMMON)', '*(COMMON) . = ALIGN(16384);')
    script = script.replace('    .bss (NOLOAD)', '    .tdata : { *(.tdata .tdata.*) } :writable\n'
                           '    .tbss (NOLOAD) : { *(.tbss .tbss.*) } :writable\n    .bss (NOLOAD)')
    script += '\nASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0, "Queue TLS requires explicit integration")\n'
    linker.write_text(script, encoding='utf-8')
    exports = output/'exports.map'
    exports.write_text('{ global: artbox_framework_runtime_check; local: *; };\n', encoding='utf-8')
    elf = output/'libartbox_framework_queue.so'
    run([tool('ld.lld'), '-shared', '-z', 'defs', '--hash-style=both', '--build-id=none',
         '-z', 'max-page-size=16384', '--pack-dyn-relocs=relr', '--no-relax', '-T', linker,
         '--version-script='+str(exports), '-soname', elf.name, crt/'crtbegin_so.o',
         *objects, '--no-as-needed', *base.values(), crt/'crtend_so.o', '-o', elf], 'link')
    dynamic = run([tool('llvm-readelf'), '--dynamic', elf], 'dynamic')
    needed = re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]', dynamic)
    if needed != list(base): raise RuntimeError('Queue dependency closure changed')
    symbols = run([tool('llvm-nm'), '-D', '--defined-only', '--format=posix', elf], 'exports')
    if {row.split()[0] for row in symbols.splitlines()} != {'artbox_framework_runtime_check'}:
        raise RuntimeError('Queue implementation symbols escaped image-local scope')
    boundary = check_code(run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', elf], 'instructions'))
    packed = output/'pack'
    run([sys.executable, '-B', ROOT/'tools/wrap_dynamic.py', elf, packed], 'pack')
    notices = {name:(verify(queue/name, expected), expected) for name,expected in build['notices'].items()}
    source_inputs = {'queue':verify(queue/'corresponding-source.zip', build['source_bundle_sha256']),
                     'looper':verify(args.looper_dir/'corresponding-source.zip', looper['source_bundle_sha256'])}
    with zipfile.ZipFile(source_inputs['looper']) as archive:
        for name,expected in looper['notices'].items():
            if Path(name).name != name: raise RuntimeError('Invalid Looper notice name')
            path = output/'notices'/name; path.parent.mkdir(exist_ok=True)
            path.write_bytes(archive.read('notices/'+name))
            if name in notices and notices[name][1] != expected: raise RuntimeError('Conflicting queue notices')
            notices[name] = (verify(path, expected), expected)
    primary = notices.pop('FRAMEWORK-NOTICE.txt')
    frameworks = {}
    for target in ('macos','ios'):
        _,frameworks[target] = prepare(packed, output/target, target, *primary, notices,
                                      name='ARTBoxFrameworkQueue', notice_name='FRAMEWORK-NOTICE.txt')
    project = run(['git','ls-files','scripts/link_framework_queue.py','scripts/dynamic_bundle.py',
        'scripts/guest_bundle.py','scripts/icu_guest_link.py','scripts/bionic_adapt.py','scripts/ndk.py',
        'scripts/build_binder.py','scripts/build_art_classlib.py','scripts/environment.py',
        'scripts/art_native_tls.py','scripts/tls_adapt.py','tools/wrap_dynamic.py',
        'third_party/bionic/m2-objects.json','fixtures/bionic-dynamic/image.ld',
        'docs/framework.md','LICENSE','THIRD_PARTY.md'], 'sources').splitlines()
    source_bundle = output/'corresponding-source.zip'
    with zipfile.ZipFile(source_bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name,path in source_inputs.items(): archive.write(path, 'inputs/'+name+'-source.zip')
        for name,record in [('queue',build),('looper',looper),('art',art),('icu',icu)]:
            archive.writestr('inputs/'+name+'.json', json.dumps(record, indent=2))
        for name in project: archive.write(ROOT/name, 'artbox/'+name)
        archive.write(linker, 'generated/image.ld'); archive.write(exports, 'generated/exports.map')
    report = dict(project_commit=revision, working_tree_dirty=False, input_revision=revision,
        scope='Signed original MessageQueue JNI and real libutils; ART execution is a separate gate',
        commands=commands, project_sources={name:digest(ROOT/name) for name in project},
        source_bundle_sha256=digest(source_bundle), framework_name='ARTBoxFrameworkQueue',
        elf_sha256=digest(elf), elf_bytes=elf.stat().st_size, needed=needed, boundary=boundary,
        frameworks=frameworks, base_inputs={name:digest(path) for name,path in base.items()},
        dex=build['dex'], core_classes_sha256=build['core_classes_sha256'],
        crt_objects={p.name:digest(p) for p in (crt/'crtbegin_so.o',crt/'crtend_so.o')},
        runtime_executed=False, device_execution_verified=False)
    save(artifacts/'m5-framework-queue-link.json', report)
    print('Original queue JNI and callers signed for macOS and iOS 15; ART execution pending')


if __name__ == '__main__': main()
