"""Build original AOSP MessageQueue Java/JNI inputs for the managed framework test."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

from environment import ROOT, environment
from sources import obtain, obtain_files
from build_art_classlib import dex_envelope, digest, run, save, write_zip
from build_binder import verify_object_code, ndk_notices
from ndk import obtain as obtain_ndk, REVISION as NDK_REVISION

CATALOG = ROOT/'third_party/framework/sources.json'
PROFILE = ROOT/'third_party/framework/queue.json'
PROJECT = ['LICENSE', 'THIRD_PARTY.md', 'docs/framework.md', 'scripts/build_framework_queue.py',
           'scripts/build_art_classlib.py', 'scripts/build_binder.py', 'scripts/environment.py',
           'scripts/sources.py', 'scripts/ndk.py', 'scripts/jdk.py', 'tests/test_framework_queue.py',
           'third_party/framework/sources.json', 'third_party/framework/queue.json',
           'fixtures/framework-queue/check.h', 'fixtures/framework-queue/check.cpp',
           'fixtures/framework-queue/runtime.cpp', 'fixtures/art-runtime/extension.h',
           'fixtures/art-runtime/record.h', 'fixtures/art-runtime/record_guest.h',
           'core/include/artbox/vm.h', 'core/include/artbox/managed_reference.h']


def verify_input(path, pin):
    data = path.read_bytes()
    if len(data) != pin['bytes'] or hashlib.sha256(data).hexdigest() != pin['sha256']:
        raise ValueError('Framework input differs from its size/hash pin: ' + str(path))
    if 'git_blob' in pin and hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() != pin['git_blob']:
        raise ValueError('Framework input differs from its Git blob: ' + str(path))
    return data


def readonly_flag(data):
    match = re.fullmatch(rb'\s*flag_value\s*\{\s*package:\s*"android\.os"\s*'
        rb'name:\s*"message_queue_tail_tracking"\s*state:\s*(ENABLED|DISABLED)\s*'
        rb'permission:\s*READ_ONLY\s*\}\s*', data)
    if not match: raise ValueError('Require the exact read-only MessageQueue release flag')
    return match[1] == b'ENABLED'


def validate_classes(names, expected):
    if len(names) != len(set(names)) or set(names) != set(expected):
        raise ValueError('Framework payload contains missing, duplicate or unreviewed classes')


def fetch(pin, path, supplied=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    if supplied is not None:
        verify_input(supplied, pin)
        if supplied.resolve() != path.resolve(): shutil.copyfile(supplied, path)
    elif not path.exists():
        partial = path.with_suffix(path.suffix+'.part')
        if 'repository' in pin:
            url = f"https://raw.githubusercontent.com/{pin['repository']}/{pin['commit']}/{pin['path']}"
            with partial.open('wb') as out:
                subprocess.run(['gh', 'api', url, '-H', 'Authorization:'], stdout=out, check=True)
        else:
            with urllib.request.urlopen(pin['url'], timeout=60) as response, partial.open('wb') as out:
                shutil.copyfileobj(response, out, 1024*1024)
        verify_input(partial, pin)
        partial.replace(path)
    verify_input(path, pin)
    return path


def core_jar(directory):
    result = json.loads((directory/'result.json').read_bytes())
    report = result['report']
    attempt = result['attempt']
    if not re.fullmatch(r'attempt-[A-Za-z0-9_-]+', attempt):
        raise ValueError('Invalid class-library attempt directory')
    path = directory/attempt/'implementation-classes.jar'
    verify_input(path, report['artifacts']['implementation-classes.jar'])
    if report['configuration_sha256'] != digest(ROOT/'third_party/art/classlib.json'):
        raise ValueError('Class-library compiler configuration changed')
    catalog = json.loads((ROOT/'third_party/sources.json').read_bytes())
    config = json.loads((ROOT/'third_party/art/classlib.json').read_bytes())
    expected = {name:catalog[name] for name in config['java_sources']+config['build_sources']}
    if report['sources'] != expected:
        raise ValueError('Class-library upstream source selection changed')
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if (report['class_files'] != config['expected_class_files'] or
                len(names) != len(set(names)) or len(names) != report['class_files'] or
                any(name+'.class' not in names for name in config['required_classes'])):
            raise ValueError('Incomplete AOSP boot compiler classes')
    return path, report


def build(args):
    catalog = json.loads(CATALOG.read_bytes()); profile = json.loads(PROFILE.read_bytes())
    cache = Path(os.environ['ARTBOX_CACHE_DIR'])
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR'])/'m5/framework-queue').resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Isolate attempts so failed compilers cannot leave stale classes in a payload.
    attempt = Path(tempfile.mkdtemp(prefix='attempt-', dir=output))
    framework = obtain_files('framework-queue', catalog['framework-queue'], cache)
    inputs = cache/'framework-inputs'
    reference = fetch(profile['compile_reference'], inputs/'android-all-15-robolectric-12468137.jar', args.reference_jar)
    pom = fetch(profile['reference_pom'], inputs/'android-all-15-robolectric-12468137.pom')
    release = fetch(profile['release_flag'], inputs/'message_queue_tail_tracking-ap3a.textproto')
    enabled = readonly_flag(release.read_bytes())
    boot, boot_report = core_jar(args.classlib_dir.resolve())
    commands = []
    def command(words, log):
        commands.append(list(map(str, words)))
        return run(words, attempt/log)
    java_home = args.java_home
    distribution = None
    if args.fetch_jdk:
        from jdk import obtain as obtain_jdk
        java_home, distribution = obtain_jdk()
    if java_home is None:
        java_home = next((Path(os.environ[k]) for k in ('ARTBOX_JAVA_HOME', 'JAVA_HOME') if os.environ.get(k)), None)
    if java_home is None: raise ValueError('Supply --java-home, ARTBOX_JAVA_HOME or --fetch-jdk')
    java = java_home/'bin'/('java.exe' if os.name == 'nt' else 'java')
    vm = ['-Xmx768m', '-XX:-UsePerfData', '-Dfile.encoding=UTF-8',
          '-Duser.home='+str(cache/'java-home'), '-Djava.io.tmpdir='+os.environ['ARTBOX_TEMP_DIR'],
          '-XX:ErrorFile='+str(attempt/'java-error-%p.log')]
    version = command([java, *vm, '-m', 'jdk.compiler/com.sun.tools.javac.Main', '-version'], 'javac-version.log').strip()
    if not re.fullmatch(r'javac 17(?:\.[0-9]+)*(?:[+-][^\s]+)?', version): raise ValueError('Framework requires JDK 17')
    generated, classes, empty = [attempt/n for n in ('generated', 'classes', 'empty')]
    for directory in (generated, classes, empty): directory.mkdir()
    declarations = (framework/'core/java/android/os/flags.aconfig').read_text(encoding='utf-8')
    names = re.findall(r'^\s+name:\s*"([a-z0-9_]+)"', declarations, re.M)
    if 'message_queue_tail_tracking' not in names: raise ValueError('Queue flag declaration is missing')
    flags = generated/'Flags.java'
    flags.write_text('// Generated from the pinned framework declarations and AP3A release value.\n'
        'package android.os; public final class Flags {\n'+''.join(
        'public static final String FLAG_'+name.upper()+' = "android.os.'+name+'";\n' for name in names)+
        'public static boolean messageQueueTailTracking() { return '+str(enabled).lower()+'; }\n}\n', encoding='utf-8')
    arguments = ['-encoding', 'UTF-8', '-source', str(profile['java_language_level']), '-target', str(profile['java_language_level']),
        '-g', '-XDstringConcat=inline', '-bootclasspath', str(boot), '-classpath', str(reference),
        '-sourcepath', str(empty), '-proc:none', '-d', str(classes),
        str(framework/'core/java/android/os/MessageQueue.java'), str(flags)]
    argfile = attempt/'javac.args'
    argfile.write_text('\n'.join('"'+a.replace('\\', '\\\\').replace('"', '\\"')+'"' for a in arguments)+'\n', encoding='utf-8')
    command([java, *vm, '-m', 'jdk.compiler/com.sun.tools.javac.Main', '@'+str(argfile)], 'javac.log')
    class_files = {p.relative_to(classes).as_posix():p for p in classes.rglob('*.class')}
    validate_classes(list(class_files), profile['java_classes'])
    jar = attempt/'queue-classes.jar'; write_zip(jar, class_files)
    r8 = obtain('r8-classlib')
    dex_archive = attempt/'queue-dex.zip'
    command([java, *vm, '-XX:TieredStopAtLevel=1', '-cp', r8/'r8.jar', 'com.android.tools.r8.D8',
        '--android-platform-build', '--min-api', str(profile['android_min_api']), '--thread-count', '1',
        '--release', '--lib', boot, '--classpath', reference, '--output', dex_archive, jar], 'd8.log')
    if 'Warning:' in (attempt/'d8.log').read_text(encoding='utf-8'): raise ValueError('D8 diagnostic requires review')
    with zipfile.ZipFile(dex_archive) as archive:
        if archive.namelist() != ['classes.dex']: raise ValueError('Unexpected framework DEX output')
        data = archive.read('classes.dex')
    dex = attempt/'framework-queue.dex'; dex.write_bytes(data)
    envelope = dex_envelope(dex.name, data)
    if envelope['classes'] != len(profile['java_classes']): raise ValueError('Unreviewed synthesized DEX classes')

    native_catalog = json.loads((ROOT/'third_party/binder/native-sources.json').read_bytes())
    common = json.loads((ROOT/'third_party/sources.json').read_bytes())
    selection = {name:native_catalog[name] for name in ('binder-utils', 'binder-system')}
    selection['art-nativehelper-runtime'] = json.loads((ROOT/'third_party/art/native-library-sources.json').read_bytes())['art-nativehelper-runtime']
    selection.update({name:common[name] for name in ('libbase-dex', 'liblog-dex')})
    sources = {name:obtain_files(name, spec, cache) for name,spec in selection.items()}
    graph = json.loads((ROOT/'third_party/binder/native-libraries.json').read_bytes())
    includes = [sources[name]/path for name in sources for path in graph['includes'].get(name, [])]
    helper = sources['art-nativehelper-runtime']
    includes += [helper/p for p in ('include', 'include_jni', 'header_only_include', 'include_platform', 'include_platform_header_only')]
    includes += [framework/'core/jni', framework/'core/jni/include']
    includes += [ROOT/'core/include', ROOT/'fixtures/art-runtime']
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32':'windows-x86_64', 'darwin':'darwin-x86_64', 'linux':'linux-x86_64'}[sys.platform]
    tc = ndk/'toolchains/llvm/prebuilt'/host
    tool = lambda name:tc/'bin'/(name+('.exe' if os.name == 'nt' else ''))
    cpp = ['--target=aarch64-linux-android35', '-std=c++20', '-O1', '-DNDEBUG', '-fPIC',
        '-fno-exceptions', '-fno-rtti', '-ffixed-x18', '-ffixed-x27', '-ffixed-x28',
        '-ffunction-sections', '-fdata-sections', '-march=armv8-a', '-mno-outline-atomics',
        '-ffile-prefix-map='+str(ROOT)+'=.', '-fdebug-prefix-map='+str(ROOT)+'=.',
        '-DANDROID_UTILS_CALLSTACK_ENABLED=0', '-DANDROID_BASE_UNIQUE_FD_DISABLE_IMPLICIT_CONVERSION',
        *[word for p in includes for word in ('-I', str(p))]]
    compiler = command([tool('clang++'), '--version'], 'native-compiler.log').strip()
    native = {}
    for name, source in [('MessageQueue', framework/'core/jni/android_os_MessageQueue.cpp'),
                         ('check', ROOT/'fixtures/framework-queue/check.cpp'),
                         ('runtime', ROOT/'fixtures/framework-queue/runtime.cpp')]:
        obj = attempt/(name+'.o')
        command([tool('clang++'), *cpp, '-c', source, '-o', obj], name+'-compile.log')
        disassembly = command([tool('llvm-objdump'), '-d', '--disassemble-zeroes', '--no-show-raw-insn', obj], name+'-code.log')
        boundary = verify_object_code(obj, disassembly)
        imports = command([tool('llvm-nm'), '-u', obj], name+'-imports.log').splitlines()
        native[obj.name] = dict(sha256=digest(obj), boundary=boundary, imports=imports)
    notices = {'FRAMEWORK-NOTICE.txt':framework/'NOTICE', 'R8-LICENSE.txt':r8/'LICENSE', 'R8-NOTICE.txt':r8/'NOTICE',
               'ARTBOX-LICENSE.txt':ROOT/'LICENSE', **ndk_notices(ndk, tc)}
    notices.update({name+'-NOTICE.txt':sources[name]/spec['notice'] for name,spec in selection.items()})
    for name,path in notices.items(): shutil.copyfile(path, attempt/name)
    project = PROJECT + ['third_party/binder/native-sources.json', 'third_party/binder/native-libraries.json',
        'third_party/art/native-library-sources.json', 'third_party/art/classlib.json', 'third_party/sources.json',
        'third_party/jdk.json']
    source_files = {'artbox/'+name:ROOT/name for name in project}
    source_files.update({'upstream/framework/'+item['path']:framework/item['path'] for item in catalog['framework-queue']['files']})
    source_files.update({'upstream/'+name+'/'+item['path']:sources[name]/item['path'] for name,spec in selection.items() for item in spec['files']})
    source_files.update({'generated/Flags.java':flags, 'metadata/release-flag.textproto':release,
                         'metadata/compile-reference.pom':pom})
    source_bundle = attempt/'corresponding-source.zip'; write_zip(source_bundle, source_files)
    report = dict(schema=1, scope=profile['scope'], project_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        working_tree_dirty=bool(subprocess.check_output(['git','status','--porcelain'],text=True).strip()),
        sources=catalog, native_sources=selection, profile_sha256=digest(PROFILE), jdk=version, jdk_distribution=distribution,
        commands=commands, native_compiler=compiler, ndk_revision=NDK_REVISION, r8_jar_sha256=digest(r8/'r8.jar'),
        core_classes_sha256=digest(boot), core_producer=boot_report['project_commit'],
        reference_sha256=digest(reference), reference_pom_sha256=digest(pom), release_flag_enabled=enabled,
        generated_sha256=digest(flags), class_files={name:digest(p) for name,p in class_files.items()}, dex=envelope,
        native_objects=native,
        project_sources={name:digest(ROOT/name) for name in project},
        source_bundle_sha256=digest(source_bundle), notices={name:digest(p) for name,p in notices.items()},
        runtime_executed=False, aosp_verifier_executed=False, compile_reference_packaged=False)
    save(attempt/'report.json', report)
    save(output/'result.json', dict(attempt=attempt.name, report=report))
    print('Original MessageQueue: six source-built DEX classes and audited Android ARM64 JNI object; runtime pending')
    print(output/'result.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--classlib-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--reference-jar', type=Path, help='Existing exact pinned compile-reference JAR')
    parser.add_argument('--ndk-root', type=Path)
    jdk = parser.add_mutually_exclusive_group()
    jdk.add_argument('--java-home', type=Path)
    jdk.add_argument('--fetch-jdk', action='store_true')
    args = parser.parse_args()
    os.environ.update(environment())
    build(args)


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('ARTBox framework queue: '+str(error), file=sys.stderr); sys.exit(1)
