"""Build the selected AOSP ART runtime and a native Linux reference harness.

Compilation and linking do not establish ART startup or DEX execution."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import platform
import re
import subprocess
import time
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from environment import environment
from art_runtime_sources import obtain_runtime_sources
from art_host_adapt import adapt_sources
from art_runtime_policy import without_rust_demangler
from sources import obtain
from ndk import obtain as obtain_ndk
from test_dex_loader import time_source, LIBBASE_UNITS, LOG_UNITS, ZIP_UNITS

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,data):p.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
def run(command,log):
    result=subprocess.run(list(map(str,command)),capture_output=True,encoding='utf-8')
    log.write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError(f'Command failed; see {log}')
    return result.stdout

def check_stack_initialization(cxx, flags, output):
    """Verify AOSP's stack initialization policy and a deterministic negative control."""
    fixture=ROOT/'fixtures/art-runtime/stack_initialization.cpp'
    cases=[]
    for name,extra,expected,message in [
        ('zero',[],0,'stack initialization: 5/5 zero'),
        ('pattern',['-ftrivial-auto-var-init=pattern'],42,'stack initialization: 5/5 nonzero')]:
        binary=output/('stack-initialization-'+name+('.exe' if os.name=='nt' else ''))
        command=list(map(str,[cxx,*flags,*extra,fixture,'-o',binary]))
        run(command,output/('stack-initialization-'+name+'-build.log'))
        result=subprocess.run([str(binary)],capture_output=True,encoding='utf-8')
        (output/('stack-initialization-'+name+'-test.log')).write_text(
            result.stdout+result.stderr,encoding='utf-8')
        passed=result.returncode==expected and result.stdout.strip()==message and not result.stderr
        cases.append({'name':name,'command':command,'binary_sha256':digest(binary),
                      'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr,
                      'passed':passed})
        record={'fixture_sha256':digest(fixture),'cases':cases}
        save(output/'stack-initialization.json',record)
        if not passed:raise RuntimeError('Compiler stack initialization policy failed: '+name)
    return record

def preserve_sources(output, sources, generated, toolchain=None):
    """Retain original notices, selected sources and build inputs with binary outputs."""
    specs=json.loads((ROOT/'third_party/sources.json').read_text(encoding='utf-8'))
    specs.update(json.loads((ROOT/'third_party/art/runtime-sources.json').read_text(encoding='utf-8')))
    files={}
    notices=output/'notices';notices.mkdir(exist_ok=True)
    for name,source in sources.items():
        spec=specs[name]
        if 'files' in spec:
            selected=[source/entry['path'] for entry in spec['files']]
        else:
            # The Android profile also needs Bionic's platform headers. Preserve
            # their per-file terms along with its complete reviewed NOTICE.
            selected=[p for p in source.rglob('*') if p.is_file() and p.suffix in ['.h','.inc','.def']]
            selected.append(source/spec['notice'])
        for path in selected:
            files['upstream/'+name+'/'+path.relative_to(source).as_posix()]=path
        notice=source/spec['notice']
        if digest(notice)!=spec['notice_sha256']:raise RuntimeError('Runtime notice changed: '+name)
        (notices/(name+'.txt')).write_bytes(notice.read_bytes())
    (notices/'ARTBOX-LICENSE.txt').write_bytes((ROOT/'LICENSE').read_bytes())
    if toolchain:
        notice=toolchain/'NOTICE'
        expected=json.loads((ROOT/'third_party/bionic/builtins.json').read_text(encoding='utf-8'))['notice_sha256']
        if digest(notice)!=expected:raise RuntimeError('NDK runtime notice differs from its reviewed pin')
        (notices/'NDK-NOTICE.txt').write_bytes(notice.read_bytes())
    project=['LICENSE','THIRD_PARTY.md','docs/DECISIONS.md','docs/m3-runtime-build.md','docs/m3-runtime-startup.md',
      'third_party/sources.json','third_party/art/runtime-sources.json','third_party/art/runtime-build.json',
      'third_party/art/adapters/no_jit.cpp','third_party/art/adapters/artbox_host_stack.h',
      'third_party/art/host-build-boundary.json','third_party/bionic/builtins.json',
      'fixtures/art-runtime/linux_reference.cpp','fixtures/art-runtime/managed_checks.cpp',
      'fixtures/art-runtime/RuntimeChecks.java','fixtures/art-runtime/RuntimeChecksHost.java',
      'fixtures/art-runtime/host_strlcpy.cpp',
      'fixtures/art-runtime/stack_initialization.cpp',
      'platform/linux/no_codegen.h','fixtures/art-runtime/codegen_policy.cpp']
    project += ['docs/m3-high-heap-runtime.md','third_party/art/managed-storage-boundary.json',
      'third_party/art/interpreter-arguments-boundary.json','third_party/art/managed-window-boundary.json',
      'third_party/art/class-table-boundary.json',
      'third_party/art/adapters/artbox_art_heap.h','third_party/art/adapters/managed_heap.cpp',
      'fixtures/art-references/artbox_art_reference_bridge.h','fixtures/art-runtime/heap_window.cpp',
      'core/include/artbox/vm.h','core/include/artbox/managed_reference.h',
      'core/src/vm.cpp','core/src/managed_reference.c',
      'platform/include/artbox/native_vm.h','platform/native_vm.c']
    project += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'scripts').glob('*.py'))]
    for name in project:files['artbox/'+name]=ROOT/name
    for path in generated:files['generated/'+path.relative_to(output).as_posix()]=path
    for path in notices.iterdir():files['notices/'+path.name]=path
    archive=output/'corresponding-source.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as bundle:
        for name,path in sorted(files.items()):
            entry=zipfile.ZipInfo(name,date_time=(1980,1,1,0,0,0))
            entry.compress_type=zipfile.ZIP_DEFLATED
            entry.external_attr=0o100644<<16
            bundle.writestr(entry,path.read_bytes())
    return {'source_bundle_sha256':digest(archive),'source_bundle_bytes':archive.stat().st_size,
      'sources':{name:specs[name] for name in sources},
      'project_sources':{name:digest(ROOT/name) for name in project},
      'generated_sources':{p.relative_to(output).as_posix():digest(p) for p in generated},
      'notices':{p.name:digest(p) for p in notices.iterdir()}}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',choices=['android','linux'],default='linux' if sys.platform=='linux' else 'android')
    parser.add_argument('--build-dir',type=Path)
    parser.add_argument('--ndk-root',type=Path)
    parser.add_argument('--cxx',default=os.environ.get('CXX','clang++'))
    parser.add_argument('--cc',default=os.environ.get('CC','clang'))
    parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--all',action='store_true',help='Compile all groups instead of a four-unit preflight')
    parser.add_argument('--link',action='store_true',help='Link the complete native Linux reference after compilation')
    parser.add_argument('--managed-window',action='store_true',
                        help='Use the checked heap-relative representation and owned high-address heap')
    args=parser.parse_args()
    if args.jobs<1:parser.error('--jobs must be positive')
    if args.link and (not args.all or args.profile!='linux'):
        parser.error('--link requires --all --profile linux')
    if args.profile=='linux' and (sys.platform!='linux' or platform.machine().lower() not in ['arm64','aarch64']):
        parser.error('The Linux reference requires a native ARM64 Linux host')
    os.environ.update(environment())
    output=args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR'])/'m3/runtime-build'/args.profile
    output.mkdir(parents=True,exist_ok=True)
    sources=obtain_runtime_sources()
    for name in ['libbase-dex','liblog-dex','ziparchive-dex','fmtlib-references','jni-dex','property-info']:
        sources[name]=obtain(name)
    art=sources['art-runtime']
    graph=json.loads((ROOT/'third_party/art/runtime-build.json').read_text(encoding='utf-8'))
    abi=[];toolchain=None
    if args.profile=='android':
        ndk=obtain_ndk(args.ndk_root)
        host={'win32':'windows-x86_64','darwin':'darwin-x86_64','linux':'linux-x86_64'}[sys.platform]
        toolchain=ndk/'toolchains/llvm/prebuilt'/host
        suffix='.exe' if os.name=='nt' else ''
        cxx=str(toolchain/'bin'/('clang++'+suffix));cc=str(toolchain/'bin'/('clang'+suffix))
        abi=['--target=aarch64-linux-android35','-U__ANDROID__']
        sources['bionic']=obtain('bionic')
    else:cxx,cc=args.cxx,args.cc
    base=[*abi,'-O1','-DNDEBUG','-fPIC','-ftrivial-auto-var-init=zero','-march=armv8-a','-mno-outline-atomics',
          '-ffixed-x18','-ffixed-x27','-ffixed-x28','-ffunction-sections','-fdata-sections']
    cxx_flags=['-std=c++20','-fno-exceptions','-fno-rtti','-Wno-invalid-offsetof']
    host_adaptation=[];host_probe=None;host_generated=[]
    if args.profile=='linux' or args.managed_window:
        selection=json.loads((ROOT/'third_party/art/runtime-sources.json').read_text(encoding='utf-8'))['art-runtime']['files']
        patch=json.loads((ROOT/'third_party/art/host-build-boundary.json').read_text(encoding='utf-8')) if args.profile=='linux' else {'files':[]}
        if args.managed_window:
            from art_managed_adapt import managed_boundary
            patch['files'].extend(managed_boundary())
        host_art=output/'host-source'
        host_adaptation=adapt_sources(art,host_art,selection,patch)
        art=host_art
        host_generated=[art/item['path'] for item in host_adaptation]
    if args.profile=='linux':
        # Test the actual compiler/libc pair, not an assumed libc version.
        fixture=ROOT/'fixtures/art-runtime/host_strlcpy.cpp'
        probe=output/'system-strlcpy'
        command=list(map(str,[cxx,*base,*cxx_flags,fixture,'-o',probe]))
        result=subprocess.run(command,capture_output=True,encoding='utf-8')
        (output/'system-strlcpy-build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        has_strlcpy=result.returncode==0
        host_probe={'command':command,'system_compile_exit':result.returncode,'system_has_strlcpy':has_strlcpy}
        if has_strlcpy:
            host_probe['system_cases']=run([probe],output/'system-strlcpy-test.log').strip()
        actual=output/'art-strlcpy'
        flags=['-DARTBOX_TEST_ART_STRLCPY','-I',art/'libartbase']
        if has_strlcpy:flags.append('-DARTBOX_SYSTEM_HAS_STRLCPY')
        run([cxx,*base,*cxx_flags,*flags,fixture,'-o',actual],output/'art-strlcpy-build.log')
        host_probe['art_cases']=run([actual],output/'art-strlcpy-test.log').strip()
    defines=['-DART_PAGE_SIZE_AGNOSTIC','-DSTATIC_LIB','-DART_STATIC_LIBARTBASE',
      '-DART_BASE_ADDRESS=0x70000000','-DART_BASE_ADDRESS_MIN_DELTA=(-0x1000000)',
      '-DART_BASE_ADDRESS_MAX_DELTA=0x1000000','-DFMT_HEADER_ONLY','-DBUILDING_LIBART',
      '-DART_DEFAULT_GC_TYPE_IS_SS','-DART_USE_TLAB=1','-DART_FRAME_SIZE_LIMIT=1736',
      '-DUSE_D8_DESUGAR=1',
      '-DART_CLANG_PATH="clang"','-D_FILE_OFFSET_BITS=64','-D_LARGEFILE64_SOURCE','-D_POSIX_C_SOURCE=200809L',
      '-DZIPARCHIVE_DISABLE_CALLBACK_API=1','-DINCFS_SUPPORT_DISABLED=1','-DZLIB_CONST']
    defines+=['-DART_STACK_OVERFLOW_GAP_'+arch+'=8192' for arch in ['arm','arm64','riscv64','x86','x86_64']]
    if host_probe and host_probe['system_has_strlcpy']:defines.append('-DARTBOX_SYSTEM_HAS_STRLCPY')
    if args.managed_window:defines.append('-DARTBOX_MANAGED_WINDOW')
    roots=['runtime','libartbase','libartbase/base','libdexfile','libdexfile/external/include',
      'libartpalette/include','libprofile','libelffile','libnativebridge/include','libnativeloader/include',
      'sigchainlib','cmdline','tools/cpp-define-generator','odrefresh/include','compiler/export']
    paths=[output,ROOT/'third_party/art/adapters',*[art/p for p in roots],sources['libbase-dex']/'include',sources['liblog-dex']/'liblog/include',
      sources['fmtlib-references']/'include',sources['jni-dex']/'include_jni',sources['ziparchive-dex']/'include',
      sources['ziparchive-dex']/'incfs_support/include',sources['property-info']/'libcutils/include',
      sources['art-tinyxml2'],sources['art-dlmalloc'],sources['art-nativehelper']/'header_only_include',
      sources['art-nativehelper']/'include_platform_header_only',sources['art-nativehelper']/'include',
      sources['art-nativehelper']/'include_platform',sources['art-unwindstack']/'libunwindstack/include',
      sources['art-lz4']/'lib',sources['art-lzma']/'C',sources['art-cpu-features']/'include']
    if args.profile=='android':paths += [sources['bionic']/'libc/platform',sources['bionic']/'libc/async_safe/include']
    if args.managed_window:paths += [ROOT/'core/include', ROOT/'platform/include', ROOT/'fixtures/art-references']
    includes=[word for p in paths for word in ['-I',str(p)]]
    runtime_flags=[*base,*cxx_flags,*defines,*includes]
    stack_probe=check_stack_initialization(cxx,runtime_flags,output) if args.profile=='linux' else None
    generator=art/'tools/cpp-define-generator'
    assembly=output/'asm_defines.s'
    run([cxx,*runtime_flags,'-UNDEBUG','-S',generator/'asm_defines.cc','-o',assembly],output/'asm-defines.log')
    header=run([sys.executable,'-B',generator/'make_header.py',assembly],output/'asm-header.log')
    (output/'asm_defines.h').write_text(header,encoding='utf-8')
    generated_sources=[assembly,output/'asm_defines.h',*host_generated]
    units=[]
    def add(name,source,flags,driver=cxx):units.append((name,source,flags,driver))
    time_unit,time_adaptation=time_source(art,output)
    generated_sources.append(time_unit)
    for group in ['runtime','support','platform']:
        for name in graph[group]:
            add(name.replace('/','-'),time_unit if name=='libartbase/base/time_utils.cc' else art/name,
                runtime_flags+(['-DZ7_ST'] if group=='support' else []))
    for library,module in [('runtime','art_operator_srcs'),('libartbase','art_libartbase_operator_srcs'),('libdexfile','dexfile_operator_srcs')]:
        bp=re.sub(r'//[^\n]*','',(art/library/'Android.bp').read_text(encoding='utf-8'))
        block=bp.split('name: "'+module+'",',1)[1]
        headers=re.findall(r'"([^"\n]+)"',re.search(r'\bsrcs:\s*\[([^\]]*)\]',block)[1])
        generated=output/(library+'-operators.cc')
        text=run([sys.executable,'-B',art/'tools/generate_operator_out.py',(art/library).as_posix(),
                  *[(art/library/p).as_posix() for p in headers]],output/(library+'-operators.log'))
        generated.write_text(text,encoding='utf-8')
        generated_sources.append(generated)
        add(library+'-operators',generated,runtime_flags)
    asm_flags=[*base,*defines,*includes]
    for name in graph['assembly']:add(name.replace('/','-'),art/name,asm_flags,cc)
    templates=sorted((art/'runtime/interpreter/mterp/arm64ng').glob('*.S'))
    nterp=output/'mterp_arm64ng.S'
    run([sys.executable,'-B',art/'runtime/interpreter/mterp/gen_mterp.py',nterp.as_posix(),
         *[p.as_posix() for p in templates]],output/'nterp-generator.log')
    generated_sources.append(nterp)
    add('mterp_arm64ng',nterp,asm_flags,cc)
    for name in LIBBASE_UNITS:add('base-'+name,sources['libbase-dex']/(name+'.cpp'),runtime_flags)
    log_flags=runtime_flags+['-DLIBLOG_LOG_TAG=1006','-DSNET_EVENT_LOG_TAG=1397638484','-DANDROID_DEBUGGABLE=0']
    for name in LOG_UNITS:add('log-'+name,sources['liblog-dex']/'liblog'/(name+'.cpp'),log_flags)
    for name in ['log_event_list','log_event_write']:
        add('log-'+name,sources['art-liblog-events']/'liblog'/(name+'.cpp'),log_flags+['-I',str(sources['liblog-dex']/'liblog')])
    for name in ZIP_UNITS:add('zip-'+Path(name).stem,sources['ziparchive-dex']/name,runtime_flags)
    for name in graph['lzma']:
        add('lzma-'+Path(name).stem,sources['art-lzma']/name,[*base,'-std=c11','-DZ7_ST','-march=armv8-a+crypto'],cc)
    for name in ['lz4','lz4hc']:add('lz4-'+name,sources['art-lz4']/'lib'/(name+'.c'),[*base,'-std=c11'],cc)
    add('tinyxml2',sources['art-tinyxml2']/'tinyxml2.cpp',base+cxx_flags)
    cpu_flags=base+['-std=c11','-DSTACK_LINE_READER_BUFFER_SIZE=1024','-DHAVE_DLFCN_H','-DHAVE_STRONG_GETAUXVAL',
      '-I',str(sources['art-cpu-features']/'include'),'-I',str(sources['art-cpu-features']/'src')]
    for name in graph['cpu-features']:add('cpu-'+Path(name).stem,sources['art-cpu-features']/name,cpu_flags,cc)
    zflags=base+['-std=c11','-DHAVE_HIDDEN','-DZLIB_CONST','-DCHROMIUM_ZLIB_NO_CASTAGNOLI',
      '-DADLER32_SIMD_NEON','-DCRC32_ARMV8_CRC32','-DINFLATE_CHUNK_READ_64LE','-DARMV8_OS_LINUX',
      '-I',str(sources['art-zlib'])]
    for name in graph['zlib']:add('zlib-'+name,sources['art-zlib']/name,zflags,cc)
    zstd_flags=base+['-std=c11','-DZSTD_HAVE_WEAK_SYMBOLS=0','-DZSTD_TRACE=0',
      '-I',str(sources['art-zstd']/'lib'),'-I',str(sources['art-zstd']/'lib/common')]
    for name in graph['zstd']:add('zstd-'+name.replace('/','-'),sources['art-zstd']/name,zstd_flags,cc)
    unwind=sources['art-unwindstack']/'libunwindstack'
    demangle=output/'Demangle.cpp';demangle.write_bytes(without_rust_demangler(unwind/'Demangle.cpp'))
    generated_sources.append(demangle)
    unwind_flags=runtime_flags+['-O0','-fno-c++-static-destructors','-DDEXFILE_SUPPORT','-DARTBOX_NO_RUST_DEMANGLE',
      '-I',str(unwind),'-I',str(sources['art-zlib']),'-I',str(sources['art-zstd']/'lib'),'-I',str(sources['art-procinfo']/'include')]
    for name in graph['unwindstack']:
        add('unwind-'+Path(name).stem,demangle if name.endswith('/Demangle.cpp') else sources['art-unwindstack']/name,unwind_flags)
    add('dex-file-supp',art/'libdexfile/external/dex_file_supp.cc',unwind_flags)
    add('no-jit',ROOT/'third_party/art/adapters/no_jit.cpp',runtime_flags)
    if args.managed_window:
        add('artbox-managed-heap',ROOT/'third_party/art/adapters/managed_heap.cpp',runtime_flags)
        # CardTable is private to libart. Keep the acceptance helper beside the
        # real implementation; do not widen AOSP's dynamic symbol visibility.
        add('artbox-heap-contract',ROOT/'fixtures/art-runtime/heap_window.cpp',runtime_flags)
        add('artbox-vm',ROOT/'core/src/vm.cpp',[x for x in runtime_flags if x!='-fno-exceptions'])
        for name,source in [('artbox-managed-reference','core/src/managed_reference.c'),
                            ('artbox-native-vm','platform/native_vm.c')]:
            add(name,ROOT/source,[*base,'-std=c11','-D_GNU_SOURCE','-I',ROOT/'core/include',
                                '-I',ROOT/'platform/include'],cc)
    if not args.all:
        names=['runtime-app_info.cc','runtime-arch-arm64-context_arm64.cc',
               'runtime-interpreter-interpreter_switch_impl0.cc','runtime-jni-java_vm_ext.cc']
        units=[u for u in units if u[0] in names]
        if len(units)!=4:raise RuntimeError('Incomplete preflight')
    if len({u[0] for u in units})!=len(units):raise RuntimeError('Duplicate object names')
    if args.all and len(units)!=(463 if args.managed_window else 458):raise RuntimeError('Complete runtime source count changed')
    record={'profile':args.profile,'runtime_executed':False,'managed_window':args.managed_window,
      'project_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
      'time_include_adaptation':time_adaptation,
      'host_source_adaptation':host_adaptation,'host_strlcpy_probe':host_probe,
      'stack_initialization_probe':stack_probe,
      'compiler_version':run([cxx,'--version'],output/'compiler-version.log')}
    record.update(preserve_sources(output,sources,generated_sources,toolchain))
    save(output/'build-inputs.json',record)
    objects=output/'objects';objects.mkdir(exist_ok=True)
    def compile_one(unit):
        name,source,flags,compiler=unit
        obj=objects/(name+'.o');obj.parent.mkdir(parents=True,exist_ok=True)
        command=list(map(str,[compiler,*flags,'-c',source,'-o',obj]))
        start=time.monotonic();result=subprocess.run(command,capture_output=True,encoding='utf-8')
        obj.with_suffix('.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        record={'unit':name,'source_sha256':digest(source),'command':command,'exit':result.returncode,'seconds':time.monotonic()-start}
        if not result.returncode:record['object_sha256']=digest(obj)
        else:print(result.stderr,flush=True)
        print(name,'PASS' if not result.returncode else 'FAIL',flush=True)
        return record
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:results=list(pool.map(compile_one,units))
    record['results']=results
    results_path=output/('all-results.json' if args.all else 'preflight-results.json')
    save(results_path,record)
    print('Passed',sum(r['exit']==0 for r in results),'/',len(results),flush=True)
    if any(r['exit'] for r in results):return 1
    if args.link:
        inputs=[]
        for item in results:
            path=objects/(item['unit']+'.o')
            if digest(path)!=item['object_sha256']:raise RuntimeError('Runtime object changed before link')
            inputs.append(path)
        response=output/'objects.rsp'
        response.write_text('\n'.join('"'+p.as_posix()+'"' for p in inputs)+'\n',encoding='utf-8')
        library=output/'libart.so'
        start=time.monotonic()
        run([cxx,'-shared','-Wl,-z,defs','-Wl,-soname,libart.so','@'+str(response),
             '-pthread','-ldl','-lm','-o',library],output/'runtime-link.log')
        harness=output/'art-linux-reference'
        harness_flags=[flag for flag in runtime_flags if flag!='-DBUILDING_LIBART']
        harness_command=[cxx,*harness_flags,'-I',ROOT/'platform/linux',
             ROOT/'fixtures/art-runtime/linux_reference.cpp',ROOT/'fixtures/art-runtime/managed_checks.cpp',
             library,'-Wl,-rpath,$ORIGIN','-pthread','-ldl','-o',harness]
        run(harness_command,output/'harness-link.log')
        record['link']={'seconds':time.monotonic()-start,'runtime_executed':False,
          'harness_command':list(map(str,harness_command)),
          'artifacts':{p.name:{'bytes':p.stat().st_size,'sha256':digest(p)} for p in [library,harness]}}
        save(results_path,record)
        print('Native Linux runtime and harness linked; execution is a separate required check',flush=True)
    return 0

if __name__=='__main__':raise SystemExit(main())
