"""Compile original servicemanager and the tested ARTBox policy boundary for native integration."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import time
from environment import ROOT, environment
from sources import obtain, obtain_files
from ndk import obtain as obtain_ndk
from build_binder import (bundle_sources, digest, ndk_notices, read_json, run,
                          symbol_inventory, verify_aidl, verify_object_code)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aidl-dir',required=True,type=Path)
    parser.add_argument('--ndk-root',type=Path)
    parser.add_argument('--build-dir',type=Path)
    parser.add_argument('--jobs',type=int,default=2)
    args=parser.parse_args()
    if args.jobs<1: parser.error('--jobs must be positive')
    os.environ.update(environment())
    output=(args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR'])/'m4/servicemanager').resolve()
    output.mkdir(parents=True,exist_ok=True)
    report=output/'build.json'
    if report.exists(): report.unlink()
    aidl=args.aidl_dir.resolve(); generation=verify_aidl(aidl)
    cache=Path(os.environ['ARTBOX_CACHE_DIR'])
    catalog=read_json(ROOT/'third_party/binder/native-sources.json')
    sources={n:obtain_files(n,s,cache) for n,s in catalog.items()}
    selected=read_json(ROOT/'third_party/binder/service-sources.json')
    vintf=read_json(ROOT/'third_party/binder/vintf-sources.json')
    selected.update({n:vintf[n] for n in ('vintf','aidl-metadata','hidl-metadata')})
    for name,spec in selected.items():
        catalog[name]=spec; sources[name]=obtain_files('binder-'+name,spec,cache)
    common=read_json(ROOT/'third_party/sources.json')
    for name in ('binder-aidl','libbase-dex','liblog-dex','fmtlib-references'):
        catalog[name]=common[name]; sources[name]=obtain(name)
    graph=read_json(ROOT/'third_party/binder/native-libraries.json')
    ndk=obtain_ndk(args.ndk_root)
    host={'win32':'windows-x86_64','darwin':'darwin-x86_64','linux':'linux-x86_64'}[sys.platform]
    tc=ndk/'toolchains/llvm/prebuilt'/host
    suffix='.exe' if sys.platform=='win32' else ''
    tool=lambda name: tc/'bin'/(name+suffix)
    flags=['--target=aarch64-linux-android35','-O1','-DNDEBUG','-fPIC','-ffixed-x18',
           '-ffixed-x27','-ffixed-x28','-ffunction-sections','-fdata-sections','-march=armv8-a',
           '-mno-outline-atomics','-ffile-prefix-map='+str(ROOT)+'=.',
           '-fdebug-prefix-map='+str(ROOT)+'=.','-DANDROID_UTILS_CALLSTACK_ENABLED=0',
           '-DANDROID_UTILS_REF_BASE_DISABLE_IMPLICIT_CONSTRUCTION',
           '-DANDROID_BASE_UNIQUE_FD_DISABLE_IMPLICIT_CONVERSION','-DBINDER_WITH_KERNEL_IPC',
           '-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-missing-field-initializers']
    service=sources['servicemanager']/'cmds/servicemanager'
    # Provide the include spelling used by ServiceManager while retaining the
    # complete, unchanged platform ID definitions from the pinned private header.
    generated=output/'include/cutils/android_filesystem_config.h'
    generated.parent.mkdir(parents=True,exist_ok=True)
    generated.write_bytes((sources['binder-platform']/'libcutils/include/private/android_filesystem_config.h').read_bytes())
    includes=[sources[n]/p for n,paths in graph['includes'].items() for p in paths]
    includes += [sources['fmtlib-references']/'include',sources['vintf']/'include',
                 sources['aidl-metadata']/'metadata/include',sources['hidl-metadata']/'metadata/include',
                 aidl/'generated/include',output/'include',service,ROOT/'core/include',ROOT/'fixtures/servicemanager']
    inc=[v for p in includes for v in ('-I',str(p))]
    units={'main.cpp':service/'main.cpp','ServiceManager.cpp':service/'ServiceManager.cpp',
           'artbox-access.cpp':ROOT/'fixtures/servicemanager/access.cpp',
           'access-check.cpp':ROOT/'fixtures/servicemanager/access_check.cpp',
           'roles.cpp':ROOT/'fixtures/servicemanager/roles.cpp',
           'kernel-policy.c':ROOT/'fixtures/servicemanager/kernel_policy.c',
           'kernel-policy-check.cpp':ROOT/'fixtures/servicemanager/kernel_policy_check.cpp',
           'service-policy.c':ROOT/'core/src/service_policy.c'}
    started=time.monotonic()
    def compile_unit(item):
        name,source=item
        cpp=source.suffix=='.cpp'
        extra=['-std=c++20','-fno-rtti','-fno-exceptions','-Wno-c99-designator'] if cpp else ['-std=c11']
        if name=='ServiceManager.cpp':
            extra+=['-Wno-error=inconsistent-missing-override','-Wno-error=deprecated-declarations']
        obj=output/(name+'.o')
        command=[tool('clang++' if cpp else 'clang'),*flags,*extra,*inc,'-c',source,'-o',obj]
        run(command,output/(name+'.log'))
        boundary=verify_object_code(obj,run([tool('llvm-objdump'),'-d','--disassemble-zeroes',
                                           '--no-show-raw-insn',obj],output/(name+'.asm')))
        print('servicemanager: '+name,flush=True)
        return name+'.o',digest(obj),boundary,list(map(str,command))
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        compiled=list(pool.map(compile_unit,units.items()))
    archive=output/'libservicemanager.a'
    if archive.exists(): archive.unlink()
    run([tool('llvm-ar'),'rcsD',archive,*[output/n for n,_,_,_ in compiled]],output/'archive.log')
    if run([tool('llvm-ar'),'t',archive],output/'members.log').splitlines()!=[n for n,_,_,_ in compiled]:
        raise RuntimeError('Service archive differs from selected objects')
    symbols=symbol_inventory(run([tool('llvm-nm'),'--format=posix','--extern-only',archive],output/'symbols.log'))
    (output/'symbols.json').write_text(json.dumps(symbols,indent=2)+'\n',encoding='utf-8')
    project=['scripts/build_servicemanager.py','third_party/binder/service-sources.json',
             'third_party/binder/vintf-sources.json','core/include/artbox/service_policy.h',
             'core/src/service_policy.c','tests/test_service_policy.cpp','docs/DECISIONS.md',
             *[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'fixtures/servicemanager').glob('*')) if p.is_file()]]
    record=dict(project_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True,cwd=ROOT).strip(),
                working_tree_dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip()),
                scope='Original service and Access contract objects; linking and runtime acceptance remain separate',
                sources=catalog,compiled_units=len(compiled),archives={archive.name:digest(archive)},
                objects={n:s for n,s,_,_ in compiled},instruction_boundaries={n:b for n,_,b,_ in compiled},
                commands=[c for _,_,_,c in compiled],compile_seconds=time.monotonic()-started,
                unit_sources={n:digest(p) for n,p in units.items()},
                generated_headers={'include/cutils/android_filesystem_config.h':digest(generated)},
                generation_sha256=digest(aidl/'generation.json'),generated_files=generation['generated_files'],
                compiler_version=run([tool('clang++'),'--version'],output/'compiler.log').strip(),
                symbols_sha256=digest(output/'symbols.json'),required_external_symbols=len(symbols['required_external']),
                link_verified=False,servicemanager_execution_verified=False,device_execution_verified=False,
                **bundle_sources(output,catalog,sources,aidl,ndk_notices(ndk,tc),project,
                    [('generated/include/cutils/android_filesystem_config.h',generated)]))
    report.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('Original service and policy contracts built; no runtime execution claim')


if __name__=='__main__': main()
