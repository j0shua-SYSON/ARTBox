"""Run original servicemanager and native Binder peers in independent signed guest roles."""
# SPDX-License-Identifier: MIT
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
import tempfile
import zipfile
from environment import ROOT, environment
from ndk import obtain as obtain_ndk
from dynamic_bundle import prepare
from icu_guest_link import check_code


def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path): return json.loads(Path(path).read_text(encoding='utf-8'))
def verify(path, expected):
    path=Path(path)
    if not path.is_file() or digest(path)!=expected:
        raise ValueError('Service producer bytes changed: '+str(path))
    return path


def source_catalog(label, record):
    # Binder and VINTF producers record their pinned project inputs and notice
    # inventory, but do not embed a second full source catalog in build.json.
    # Reconstruct only their declared selections after verifying those inputs.
    common=read(ROOT/'third_party/sources.json')
    native=read(ROOT/'third_party/binder/native-sources.json')
    if label=='binder':
        catalog=native
        for name in ('libbase-dex','liblog-dex','binder-aidl'): catalog[name]=common[name]
    elif label=='vintf':
        catalog=read(ROOT/'third_party/binder/vintf-sources.json')
        for name in ('libbase-dex','liblog-dex','fmtlib-references'): catalog[name]=common[name]
        catalog['binder-build-reference']=native['binder-build-reference']
    else: catalog=record['sources']
    notices={name:dict(path=spec['notice'],sha256=spec['notice_sha256']) for name,spec in catalog.items()}
    if notices!=record['source_notices']:
        raise ValueError('Service source selection differs from producer notices: '+label)
    return catalog


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('native','binder','vintf','parser','guest','dependency'):
        parser.add_argument('--'+name+'-dir',required=True,type=Path)
    parser.add_argument('--build-dir',type=Path)
    parser.add_argument('--ndk-root',type=Path)
    args=parser.parse_args()
    if sys.platform!='darwin' or platform.machine().lower() not in ('arm64','aarch64'):
        parser.error('Signed service execution requires an ARM64 Mac')
    os.environ.update(environment())
    builds,artifacts=[Path(os.environ[n]) for n in ('ARTBOX_BUILD_DIR','ARTBOX_ARTIFACTS_DIR')]
    output=(args.build_dir or builds/'m4/service-guest').resolve()
    output.mkdir(parents=True,exist_ok=True)
    commands=[]
    def run(words,label):
        command=list(map(str,words)); commands.append(command)
        p=subprocess.run(command,capture_output=True,timeout=180)
        (output/(label+'.stdout')).write_bytes(p.stdout); (output/(label+'.stderr')).write_bytes(p.stderr)
        if p.returncode:
            sys.stderr.buffer.write(p.stderr)
            raise RuntimeError('Service packaging failed: '+label)
        return p.stdout.decode('utf-8')
    revision=run(['git','rev-parse','HEAD'],'revision').strip()
    if run(['git','status','--porcelain'],'worktree').strip():
        raise ValueError('Service execution requires a clean producer')
    native,binder,vintf,parser_dir,guest,deps=[p.resolve() for p in
        (args.native_dir,args.binder_dir,args.vintf_dir,args.parser_dir,args.guest_dir,args.dependency_dir)]
    records={}; bundles={}; archives=[]; notices={}
    for label,directory in [('service',native),('binder',binder),('vintf',vintf)]:
        record=read(directory/'build.json'); records[label]=record
        if record['project_commit']!=revision or record.get('working_tree_dirty',False):
            raise ValueError('Mixed or dirty service dependency: '+label)
        for name,expected in record['project_files'].items(): verify(ROOT/name,expected)
        for name,expected in record['objects'].items(): verify(directory/name,expected)
        for name,expected in record['archives'].items(): archives.append(verify(directory/name,expected))
        bundle=verify(directory/'corresponding-source.zip',record['source_bundle_sha256']); bundles[label]=bundle
        with zipfile.ZipFile(bundle) as source:
            for name,spec in source_catalog(label,record).items():
                for row in spec['files']:
                    data=source.read('upstream/'+name+'/'+row['path'])
                    if hashlib.sha256(data).hexdigest()!=row['sha256']: raise ValueError('Source archive changed')
                path=output/'notices'/(name+'.txt'); path.parent.mkdir(exist_ok=True)
                data=source.read('upstream/'+name+'/'+spec['notice'])
                if hashlib.sha256(data).hexdigest()!=spec['notice_sha256']: raise ValueError('Service notice changed')
                if path.exists() and path.read_bytes()!=data: raise ValueError('Conflicting source notice')
                path.write_bytes(data); notices[path.name]=(path,spec['notice_sha256'])
            for name,expected in record['ndk_notice_hashes'].items():
                path=output/'notices'/name; path.write_bytes(source.read('notices/'+name))
                verify(path,expected); notices[name]=(path,expected)
    if records['service']['compiled_units']!=8:
        raise ValueError('Service caller selection changed')
    regex_pin=read(ROOT/'third_party/binder/regex-runtime.json')
    parser_build=read(parser_dir/'result.json')
    if parser_build['project_commit']!=revision or parser_build['regex_runtime']!=regex_pin:
        raise ValueError('Regex producer changed')
    regex=verify(parser_dir/'regex.cpp.o',regex_pin['member_sha256'])
    art=read(artifacts/'m3-art-guest-link.json')
    bionic=read(deps/'artifacts/m2-bionic-startup.json')
    math=read(deps/'artifacts/m3-art-math.json')
    loader=read(deps/'artifacts/m3-guest-loader.json')
    for label,record in [('art',art),('bionic',bionic),('math',math),('loader',loader),('regex',parser_build)]:
        if record['project_commit']!=revision: raise ValueError('Mixed native service runtime producers')
        for name,expected in record.get('project_sources',{}).items(): verify(ROOT/name,expected)
        records[label]=record
    if art['input_revision']!=revision or art['working_tree_dirty']: raise ValueError('Dirty ART producer')
    modules=[
        (deps/'build/m2/bionic-startup/libc.so',art['dependencies']['libc.so'],
         deps/'build/m2/bionic-startup/libc/macos/ARTBoxBionic.framework',bionic['images']['libc']['frameworks']['macos']),
        (guest/'libart.so',art['elf_sha256'],guest/'macos/ARTBoxRuntime.framework',art['frameworks']['macos']),
        (deps/'build/m3/art-math/libm.so',art['dependencies']['libm.so'],
         deps/'build/m3/art-math/macos/library/ARTBoxMath.framework',math['frameworks']['macos-library']),
        (deps/'build/m3/guest-loader/libdl.so',art['dependencies']['libdl.so'],
         deps/'build/m3/guest-loader/macos/libdl/ARTBoxLoaderLibdl.framework',loader['frameworks']['macos-libdl']),
        (deps/'build/m3/guest-loader/libdl_android.so',loader['binaries']['libdl_android']['sha256'],
         deps/'build/m3/guest-loader/macos/libdl_android/ARTBoxLoaderLibdlAndroid.framework',loader['frameworks']['macos-libdl_android'])]
    for elf,expected,framework,details in modules:
        verify(elf,expected); verify(framework/framework.stem,details['layout']['macho_sha256'])
        if details['entitlements'] or not details['signature_verified']: raise ValueError('Unsigned service dependency')
    ndk=obtain_ndk(args.ndk_root); tc=ndk/'toolchains/llvm/prebuilt/darwin-x86_64'
    tool=lambda name:tc/'bin'/name
    crt=tc/'sysroot/usr/lib/aarch64-linux-android/35'
    linker=output/'image.ld'
    script=(ROOT/'fixtures/bionic-dynamic/image.ld').read_text(encoding='utf-8')
    script=script.replace('KEEP(*(.init_array .init_array.*))',
        'KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))')
    script=script.replace('*(COMMON)','*(COMMON) . = ALIGN(16384);')
    script=script.replace('    .bss (NOLOAD)','    .tdata : { *(.tdata .tdata.*) } :writable\n'
        '    .tbss (NOLOAD) : { *(.tbss .tbss.*) } :writable\n    .bss (NOLOAD)')
    # The selected service does not retain BufferedTextOutput's TLS sections.
    # Reject future growth rather than emitting an empty PT_TLS declaration.
    script+='\nASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0, "Service TLS selection changed")\n'
    linker.write_text(script,encoding='utf-8')
    exports={'main','artbox_service_access_configure','artbox_service_access_check','artbox_service_access_prepare',
             'artbox_service_kernel_policy_check','artbox_service_provider','artbox_service_client'}
    export_file=output/'exports.map'
    export_file.write_text('{ global: '+'; '.join(sorted(exports))+'; local: *; };\n',encoding='utf-8')
    elf=output/'libartbox_servicemanager.so'
    # -u roots retain exported members of archives. All implementation symbols
    # stay image-local so each role has its own libbinder and service state.
    run([tool('ld.lld'),'-shared','-z','defs','--hash-style=both','--build-id=none','--gc-sections',
         '-z','max-page-size=16384','--pack-dyn-relocs=relr','--no-relax','-T',linker,
         '--version-script='+str(export_file),'-soname',elf.name,
         *[flag for name in sorted(exports) for flag in ('-u',name)],crt/'crtbegin_so.o',
         '--start-group',*archives,regex,'--end-group','--no-as-needed',*[row[0] for row in modules],
         crt/'crtend_so.o','-o',elf],'link')
    needed=re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]',run([tool('llvm-readelf'),'--dynamic',elf],'dynamic'))
    if needed!=[row[0].name for row in modules]: raise ValueError('Service dependency closure changed')
    symbols=run([tool('llvm-nm'),'-D','--defined-only','--format=posix',elf],'exports')
    if {line.split()[0] for line in symbols.splitlines()}!=exports: raise ValueError('Service exports changed')
    boundary=check_code(run([tool('llvm-objdump'),'-d','--no-show-raw-insn',elf],'instructions'))
    notices['ARTBOX-LICENSE.txt']=(ROOT/'LICENSE',digest(ROOT/'LICENSE'))
    # Preserve the source closures supplied by dependency producers. Bionic's
    # startup report and all of its binary notices are retained separately.
    for label,directory,record in [('art',guest,art),
        ('math',deps/'build/m3/art-math',math),('loader',deps/'build/m3/guest-loader',loader),('regex',parser_dir,parser_build)]:
        bundles[label]=verify(directory/'corresponding-source.zip',record['source_bundle_sha256'])
    packed=[]; module_notices=[]
    for i,(source,_,framework,details) in enumerate(modules):
        directory=output/('pack-'+str(i)); run([sys.executable,'-B',ROOT/'tools/wrap_dynamic.py',source,directory],'pack-'+str(i))
        packed.append(directory)
        module_notices.append({name:(verify(framework/name,sha),sha) for name,sha in details['notices'].items()})
    directory=output/'pack-5'; run([sys.executable,'-B',ROOT/'tools/wrap_dynamic.py',elf,directory],'pack-5')
    packed.append(directory); module_notices.append(notices)
    elfs=[row[0] for row in modules]+[elf]
    frameworks={}; paths={}
    for target in ('macos','ios'):
        for role in range(3):
            for i in range(6):
                name=f'ARTBoxServiceR{role}M{i}'; notice_map=dict(module_notices[i])
                primary_name=sorted(notice_map)[0]; primary=notice_map.pop(primary_name)
                binary,details=prepare(packed[i],output/target/name,target,*primary,notice_map,
                                       name=name,notice_name=primary_name)
                key=f'{target}-{role}-{i}'; frameworks[key]=details; paths[key]=binary
    project=run(['git','ls-files','core','platform','CMakeLists.txt','LICENSE','THIRD_PARTY.md',
        'docs/binder-build.md','docs/DECISIONS.md','fixtures/servicemanager','fixtures/bionic-dynamic/image.ld',
        'scripts','tools','tests/native_service_guest.c','.github/workflows/host-tests.yml'],'sources').splitlines()
    bundle=output/'corresponding-source.zip'
    with zipfile.ZipFile(bundle,'w',compression=zipfile.ZIP_DEFLATED) as z:
        for name,path in bundles.items(): z.write(path,'inputs/'+name+'-source.zip')
        for name,record in records.items(): z.writestr('inputs/'+name+'.json',json.dumps(record,indent=2))
        for name in project: z.write(ROOT/name,'artbox/'+name)
        z.write(linker,'generated/image.ld'); z.write(export_file,'generated/exports.map')
    runner=builds/'host/artbox_native_service'
    record=dict(project_commit=revision,working_tree_dirty=False,commands=commands,
        source_bundle_sha256=digest(bundle),project_sources={n:digest(ROOT/n) for n in project},
        elf_sha256=digest(elf),needed=needed,boundary=boundary,frameworks=frameworks,
        input_objects={label:r['objects'] for label,r in records.items() if isinstance(r.get('objects'),dict)},
        runner_sha256=digest(runner),executions={},guest_execution_verified=False,device_execution_verified=False)
    report=artifacts/'m4-service-guest.json'
    try:
        for mode,label,expected in [(1,'access',20),(2,'wrong-uid',-109),(3,'wrong-pid',-107),(0,'roles',0)]:
            root=Path(tempfile.mkdtemp(prefix=label+'-',dir=output))
            command=[str(runner),str(mode),str(root)]
            for role in range(1 if mode else 3):
                role_root=root/str(role)
                for name in ('data','system'): (role_root/name).mkdir(parents=True)
                command.append(str(role_root))
                for i in range(6): command.extend([str(paths[f'macos-{role}-{i}']),str(elfs[i])])
            commands.append(command)
            try: process=subprocess.run(command,capture_output=True,timeout=40)
            except subprocess.TimeoutExpired as error:
                (output/(label+'.stdout')).write_bytes(error.stdout or b'')
                (output/(label+'.stderr')).write_bytes(error.stderr or b'')
                record['executions'][label]=dict(timeout=True); raise
            (output/(label+'.stdout')).write_bytes(process.stdout)
            (output/(label+'.stderr')).write_bytes(process.stderr)
            result=dict(exit=process.returncode,stdout_sha256=hashlib.sha256(process.stdout).hexdigest(),
                        stderr_sha256=hashlib.sha256(process.stderr).hexdigest())
            record['executions'][label]=result
            try:
                result['native']=native_result=json.loads(process.stdout)
                exact=dict(access_cases=expected,policy_cases=5,provider=0 if mode else 32,client=0 if mode else 32,
                    roles=1 if mode else 3,independent_images=6 if mode else 18,
                    manager_retained=not bool(mode),finite_roles_cleaned=True,passed=True)
                if process.returncode or any(type(native_result.get(k)) is not type(v) or native_result[k]!=v for k,v in exact.items()):
                    raise ValueError('Service contract failed: '+label)
                if native_result['elapsed_ns']<=0 or native_result['peak_rss_bytes']<=0: raise ValueError('Missing measurement')
            except (ValueError,TypeError,KeyError):
                sys.stderr.buffer.write(process.stderr); raise
        record['guest_execution_verified']=True
    finally: report.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('Original servicemanager: registration, 32 ping/pong calls and original death notification pass')


if __name__=='__main__': main()
