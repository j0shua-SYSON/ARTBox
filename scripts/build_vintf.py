"""Cross-compile original Android VINTF, kernel-version and APEX XML dependencies.

These audited ELF archives are inputs for a future signed service link. This
script does not execute a guest, supply SELinux policy, or run servicemanager.
"""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import zipfile

from build_binder import ndk_notices, symbol_inventory, verify_object_code
from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from sources import obtain, obtain_files
from xsdc import prepare, verify_generation

CATALOG = ROOT / 'third_party/binder/vintf-sources.json'
GRAPH = ROOT / 'third_party/binder/vintf-libraries.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def run(command, log):
    process = subprocess.run(list(map(str, command)), capture_output=True, timeout=180)
    log.write_bytes(process.stdout + process.stderr)
    if process.returncode:
        raise RuntimeError('VINTF build command failed; see ' + str(log))
    return process.stdout.decode('utf-8')


def bundle_sources(output, catalog, sources, xsdc, notices):
    files = {}
    for name, spec in catalog.items():
        for row in spec['files']:
            path = sources[name] / row['path']
            if digest(path) != row['sha256']:
                raise RuntimeError('VINTF source changed during compilation: ' + row['path'])
            files['upstream/' + name + '/' + row['path']] = path
    generation = verify_generation(xsdc)
    generator_bundle = xsdc / 'corresponding-source.zip'
    if digest(generator_bundle) != generation['source_bundle_sha256']:
        raise RuntimeError('XSdc corresponding source changed')
    files['xsdc/generation.json'] = xsdc / 'generation.json'
    files['xsdc/corresponding-source.zip'] = generator_bundle
    for name in generation['generated_files']:
        files['xsdc/generated/' + name] = xsdc / 'generated' / name
    for name, path in notices.items(): files['notices/' + name] = path
    project = ['LICENSE', 'THIRD_PARTY.md', 'docs/binder-build.md',
        'scripts/build_vintf.py', 'scripts/build_binder.py', 'scripts/binder_aidl.py',
        'scripts/bionic_adapt.py', 'scripts/art_native_tls.py', 'scripts/tls_adapt.py',
        'scripts/environment.py', 'scripts/sources.py', 'scripts/ndk.py',
        'scripts/xsdc.py', 'scripts/jdk.py', 'tests/test_xsdc.py', 'tests/test_binder_build.py',
        'third_party/binder/vintf-sources.json', 'third_party/binder/vintf-libraries.json',
        'third_party/binder/xsdc.json', 'third_party/binder/native-sources.json',
        'third_party/bionic/builtins.json', 'third_party/sources.json', 'third_party/jdk.json']
    for name in project: files['artbox/' + name] = ROOT / name
    bundle = output / 'corresponding-source.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED; entry.external_attr = 0o100644 << 16
            archive.writestr(entry, path.read_bytes())
    return dict(source_bundle_sha256=digest(bundle), project_files={n:digest(ROOT/n) for n in project},
        source_notices={n:dict(path=s['notice'],sha256=s['notice_sha256']) for n,s in catalog.items()},
        ndk_notice_hashes={n:digest(p) for n,p in notices.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xsdc-dir', type=Path, help='Verified xsdc.py output; generate locally if omitted')
    parser.add_argument('--java-home', type=Path, help='Existing JDK for local generation')
    parser.add_argument('--ndk-root', type=Path)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1: parser.error('--jobs must be positive')
    if args.xsdc_dir and args.java_home: parser.error('--java-home is for local generation only')
    os.environ.update(environment())
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm4/vintf-native').resolve()
    output.mkdir(parents=True, exist_ok=True)
    result_path = output / 'build.json'
    if result_path.exists(): result_path.unlink()
    xsdc = args.xsdc_dir.resolve() if args.xsdc_dir else output / 'xsdc'
    if not args.xsdc_dir: prepare(xsdc, args.java_home)
    generation = verify_generation(xsdc)
    catalog, graph = read(CATALOG), read(GRAPH)
    sources = {n:obtain_files('binder-' + n,s,Path(os.environ['ARTBOX_CACHE_DIR'])) for n,s in catalog.items()}
    common = read(ROOT / 'third_party/sources.json')
    for name in ('libbase-dex','liblog-dex','fmtlib-references'):
        catalog[name] = common[name]; sources[name] = obtain(name)
    # Soong's warning-policy reference is already pinned for the Binder build.
    name = 'binder-build-reference'
    catalog[name] = read(ROOT/'third_party/binder/native-sources.json')[name]
    sources[name] = obtain_files(name,catalog[name],Path(os.environ['ARTBOX_CACHE_DIR']))
    ndk = obtain_ndk(args.ndk_root)
    host = {'win32':'windows-x86_64','darwin':'darwin-x86_64','linux':'linux-x86_64'}[sys.platform]
    tools = ndk / 'toolchains/llvm/prebuilt' / host / 'bin'
    suffix = '.exe' if sys.platform == 'win32' else ''
    cxx, ar, nm, objdump = [tools/(n+suffix) for n in ('clang++','llvm-ar','llvm-nm','llvm-objdump')]
    notices = ndk_notices(ndk,tools.parent)
    flags = ['--target='+graph['target'],'-std='+graph['cpp_standard'],'-O1','-DNDEBUG','-fPIC',
        '-fno-exceptions','-fno-rtti','-ffixed-x18','-ffixed-x27','-ffixed-x28',
        '-ffunction-sections','-fdata-sections','-march=armv8-a','-mno-outline-atomics',
        '-ffile-prefix-map='+str(ROOT)+'=.','-fdebug-prefix-map='+str(ROOT)+'=.',
        '-DLIBVINTF_TARGET','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
        '-Wno-error=deprecated-declarations','-Wno-error=inconsistent-missing-override',
        '-Wno-error=sign-compare']
    includes = [sources[n]/p for n,paths in graph['includes'].items() for p in paths]
    includes.append(xsdc/'generated/include')
    # Quote-only VINTF headers keep Regex.h from shadowing the NDK's <regex.h>
    # on case-insensitive hosts, without changing the original source.
    include_flags = ['-iquote',str(sources['vintf']/'include/vintf')]
    include_flags += [v for p in includes for v in ('-I',str(p))]
    jobs = []
    for library, spec in graph['libraries'].items():
        units = [(n,sources[spec['source']]/n) for n in spec['units']]
        if spec.get('generated_xsdc'):
            units += [(n,xsdc/'generated'/n) for n in generation['generated_files'] if n.endswith('.cpp')]
        for name,path in units:
            obj = output/'objects'/library/(name.replace('/','__')+'.o')
            obj.parent.mkdir(parents=True,exist_ok=True)
            jobs.append((library,name,path,obj))
    started = time.monotonic()
    def compile_unit(job):
        library,name,source,obj = job
        extra = graph['libraries'][library].get('unit_flags',{}).get(name,[])
        run([cxx,*flags,*extra,*include_flags,'-c',source,'-o',obj],obj.with_suffix('.log'))
        boundary = verify_object_code(obj,run([objdump,'-d','--disassemble-zeroes','--no-show-raw-insn',obj],obj.with_suffix('.asm')))
        print(library+': '+name,flush=True)
        return obj.relative_to(output).as_posix(),digest(obj),boundary
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled = list(pool.map(compile_unit,jobs))
    archives = {}
    for library in graph['libraries']:
        archive = output/(library+'.a')
        if archive.exists(): archive.unlink()
        members = [obj for lib,_,_,obj in jobs if lib == library]
        run([ar,'rcsD',archive,*members],output/(library+'-archive.log'))
        if run([ar,'t',archive],output/(library+'-members.log')).splitlines() != [p.name for p in members]:
            raise RuntimeError('VINTF archive membership differs from the compiled objects')
        archives[archive.name] = digest(archive)
    symbols = symbol_inventory(run([nm,'--format=posix','--extern-only',*[output/n for n in archives]],output/'symbols.log'))
    (output/'symbols.json').write_text(json.dumps(symbols,indent=2)+'\n',encoding='utf-8')
    record = dict(target=graph['target'],compiled_units=len(compiled),archives=archives,
        objects={n:s for n,s,_ in compiled},instruction_boundaries={n:b for n,_,b in compiled},
        compile_seconds=time.monotonic()-started,generation_sha256=digest(xsdc/'generation.json'),
        generated_files=generation['generated_files'],required_external_symbols=len(symbols['required_external']),
        symbols_sha256=digest(output/'symbols.json'),
        compiler_version=run([cxx,'--version'],output/'compiler.log').strip(),
        flags=[f.replace(str(ROOT),'${ARTBOX_ROOT}') for f in flags],
        unit_flags={n:s.get('unit_flags',{}) for n,s in graph['libraries'].items()},
        project_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        link_verified=False,servicemanager_execution_verified=False,device_execution_verified=False,
        **bundle_sources(output,catalog,sources,xsdc,notices))
    result_path.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(compiled_units=len(compiled),archives=len(archives),
        required_external_symbols=record['required_external_symbols'],link_verified=False)))


if __name__ == '__main__':main()
