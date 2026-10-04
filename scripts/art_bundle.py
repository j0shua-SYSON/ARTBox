"""Verify successful signed ART acceptance and stage its iOS runtime inputs."""
import sys
sys.dont_write_bytecode = True
import hashlib
import json
from pathlib import Path
import shutil
from environment import ROOT
from link_icu_guest import LIBRARIES as ICU_LIBRARIES
from link_libcore_guest import LIBRARIES as LIBCORE_LIBRARIES
sys.path.insert(0, str(ROOT / 'tools'))
from wrap_dynamic import pack_layout, verify_macho


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_acceptance(report):
    """Build/constructor success cannot replace actual managed execution."""
    try:
        require(all(report.get(key) is True for key in (
            'passed', 'runtime_invocation_attempted', 'runtime_started', 'dex_executed',
            'lifecycle_verified', 'console_verified', 'console_drop_detected', 'missing_hello_detected')),
            'ART requires every managed, console and negative-control observation')
        require(not report.get('timeout') and report['exit'] == 0 and report['missing_hello_exit'] == 1 and
                report['console_drop_exit'] == 3,
                'ART process or missing-class control failed')
        native, managed, worker = report['native'], report['managed_checks'], report['vm_worker']
        require(native['linked_images'] == 15 and native['constructors'] >= 35 and
                native['tls_modules'] >= 1 and native['registered_vms'] == 0 and
                all(native[key] is True for key in
                    ('cleanup', 'heap_binding_verified', 'runtime_started', 'dex_executed')),
                'ART native lifecycle is incomplete')
        require(all(type(native[key]) is int and native[key] > 0 for key in
                    ('startup_ns', 'managed_bytes', 'process_peak_rss_bytes', 'threads_reaped')),
                'ART requires measured execution and worker cleanup')
        require(managed['heap_checksum'] == 6496 and managed['exceptions'] == 3 and
                managed['attachments'] == 4 and type(managed['gc_before']) is int and
                type(managed['gc_after']) is int and 0 <= managed['gc_before'] < managed['gc_after'],
                'ART collection, exceptions or attachment failed')
        require(report['thread_state_checks'] == dict(threads=3, attach_cycles=4,
                tls_isolated=True, main_tls_restored=True), 'ART thread state failed')
        require(worker['primordial'] is False and worker['current_in_stack'] is True and
                worker['requested_stack_bytes'] == 4 * 1024 * 1024 and
                type(worker['reported_stack_bytes']) is int and type(worker['guard_bytes']) is int and
                0 <= worker['guard_bytes'] < worker['reported_stack_bytes'], 'ART worker stack failed')
    except (KeyError, TypeError) as error:
        raise RuntimeError('Incomplete ART acceptance report') from error


def prepare(evidence, output, revision):
    """Evidence is the merged, same-run artifact tree; output must be empty."""
    evidence, output = Path(evidence).resolve(), Path(output).resolve()
    read = lambda name: json.loads((evidence / 'artifacts' / name).read_text(encoding='utf-8'))
    names = ('m2-bionic-startup', 'm3-art-guest-link', 'm3-art-math', 'm3-guest-loader',
             'm3-icu-guest-link', 'm3-libcore-guest-link', 'm3-art-runtime-guest')
    reports = {name: read(name + '.json') for name in names}
    require(all(r['project_commit'] == revision for r in reports.values()),
            'ART inputs must come from this exact project revision')
    bionic, art, math, loader, icu, libcore, runtime = (reports[name] for name in names)
    validate_acceptance(runtime)
    for report in (art, icu, libcore):
        require(report['input_revision'] == revision and report['working_tree_dirty'] is False,
                'ART link inputs must have clean matching provenance')
    run_dir = evidence / 'build/m3/art-runtime-guest'
    require(json.loads((run_dir / 'native.stdout').read_text()) == runtime['native'],
            'ART result differs from its retained process output')
    lines = (run_dir / 'native.stderr').read_text().splitlines()
    require(all(message in lines for message in (
        'ARTBox: entering signed ART JNI_CreateJavaVM',
        'ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache',
        'hello from ARTBox ART', 'ARTBox: signed ART method returned the expected string',
        'ARTBox: signed ART lifecycle checks passed', 'ARTBox console: hello and lifecycle observed')),
        'ART log lacks the required execution observations')
    negative = (run_dir / 'missing-hello.stderr').read_text().splitlines()
    require(not (run_dir / 'missing-hello.stdout').read_bytes() and
            'ARTBox: signed ART started; switch interpreter, no JIT, no profiling cache' in negative and
            'signed ART runtime result: 3' in negative, 'ART missing-class control output changed')
    dropped = json.loads((run_dir / 'missing-console.stdout').read_text())
    require(all(dropped[key] is True for key in ('runtime_started', 'dex_executed', 'cleanup')) and
            'ARTBox console: missing guest output' in (run_dir / 'missing-console.stderr').read_text().splitlines(),
            'ART dropped-console control output changed')

    # Select the same 15-image order as the native runtime, without accepting
    # arbitrary framework names or dependencies from a supplied manifest.
    base = evidence / 'build'
    modules = [
        ('libc.so', 'ARTBoxBionic', base / 'm2/bionic-startup/libc.so',
         base / 'm2/bionic-startup/libc', bionic['images']['libc']['frameworks']),
        ('libart.so', 'ARTBoxRuntime', base / 'm3/art-guest-link/libart.so',
         base / 'm3/art-guest-link', art['frameworks']),
        ('libm.so', 'ARTBoxMath', base / 'm3/art-math/libm.so', base / 'm3/art-math',
         {p: math['frameworks'][p + '-library'] for p in ('macos', 'ios')}),
        ('libdl.so', 'ARTBoxLoaderLibdl', base / 'm3/guest-loader/libdl.so', base / 'm3/guest-loader',
         {p: loader['frameworks'][p + '-libdl'] for p in ('macos', 'ios')})]
    for report, directory, libraries in (
            (icu, 'icu-guest-link', [(row[0], row[1]) for row in ICU_LIBRARIES]),
            (libcore, 'libcore-guest-link', [(row[0], row[1]) for row in LIBCORE_LIBRARIES] +
             [('libartbox_libcore_check.so', 'ARTBoxLibcoreCheck')])):
        for elf, framework in libraries:
            item = report['libraries'][elf]
            require(item['framework_name'] == framework and
                    item['elf_sha256'] == runtime['elf_sha256'][elf], 'ART dependency identity changed')
            modules.append((elf, framework, base / 'm3' / directory / elf,
                            base / 'm3' / directory, item['frameworks']))
    require(set(runtime['elf_sha256']) == {row[0] for row in modules} and
            set(runtime['framework_sha256']) == set(runtime['elf_sha256']), 'ART image set changed')
    images = []
    for elf_name, framework, elf, directory, signed in modules:
        expected = runtime['elf_sha256'][elf_name]
        require(digest(elf) == expected, 'ART ELF changed: ' + elf_name)
        layout = pack_layout(elf.read_bytes())[2]
        for platform in ('macos', 'ios'):
            suffix = 'library' if elf_name == 'libm.so' else 'libdl' if elf_name == 'libdl.so' else ''
            bundle = directory / platform / suffix / (framework + '.framework')
            proof = signed[platform]
            require(proof['signature_verified'] is True and proof['entitlements'] == {},
                    'ART requires ordinary signed frameworks')
            require(verify_macho((bundle / framework).read_bytes(), layout) == proof['layout'],
                    'ART signed bytes or virtual layout changed: ' + framework)
            if platform == 'macos':
                require(digest(bundle / framework) == runtime['framework_sha256'][elf_name],
                        'ART device input differs from the executed library')
            for name, expected_notice in proof['notices'].items():
                require(Path(name).name == name and '/' not in name and '\\' not in name and
                        digest(bundle / name) == expected_notice, 'ART notice changed')
        images.append(dict(elf=elf_name, framework=framework, elf_sha256=expected,
                           layout=layout, frameworks=signed))

    roots = list(run_dir.glob('root-*'))
    require(len(roots) == 1 and roots[0].is_dir(), 'ART requires one retained runtime root')
    root = roots[0]
    dex = runtime['boot_dex']
    require([item['name'] for item in dex] == ['classes.dex', 'classes2.dex'], 'ART boot DEX set changed')
    resources = {('system/framework/' + item['name']): item['sha256'] for item in dex}
    resources.update({'data/hello.dex': runtime['hello_sha256'],
        'data/runtime-checks.dex': runtime['managed_fixture']['dex']['sha256'],
        'system/i18n/etc/icu/icudt75l.dat': runtime['data_sha256']})
    require(runtime['managed_fixture']['project_commit'] == revision, 'ART managed DEX revision changed')
    for name, expected in resources.items():
        require(digest(root / name) == expected, 'ART runtime resource changed: ' + name)
    require(not output.exists() or not any(output.iterdir()), 'ART staging output must be empty')
    (output / 'Resources/ELF').mkdir(parents=True, exist_ok=True)
    for image, (_, framework, elf, directory, _) in zip(images, modules):
        suffix = 'library' if elf.name == 'libm.so' else 'libdl' if elf.name == 'libdl.so' else ''
        shutil.copytree(directory / 'ios' / suffix / (framework + '.framework'),
                        output / (framework + '.framework'))
        shutil.copyfile(elf, output / 'Resources/ELF' / image['elf'])
    for name in resources:
        target = output / 'Resources/root' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    staged = dict(project_commit=revision, images=images, resources=resources,
                  acceptance=runtime, device_execution_verified=False,
                  producer_reports={name: digest(evidence / 'artifacts' / (name + '.json')) for name in names})
    (output / 'Resources/manifest.json').write_text(json.dumps(staged, indent=2) + '\n', encoding='utf-8')
    return staged


def verify_embedded(app, staged):
    """Check bytes after Xcode copied resources and frameworks into the app."""
    app = Path(app)
    resources = app / 'ARTBoxM3'
    require(json.loads((resources / 'manifest.json').read_text()) == staged, 'Embedded ART manifest changed')
    for item in staged['images']:
        name = item['framework']
        bundle = app / 'Frameworks' / (name + '.framework')
        require(digest(resources / 'ELF' / item['elf']) == item['elf_sha256'], 'Embedded ART ELF changed')
        require(verify_macho((bundle / name).read_bytes(), item['layout']) == item['frameworks']['ios']['layout'],
                'Embedded ART signed image changed')
        for notice, expected in item['frameworks']['ios']['notices'].items():
            require(digest(bundle / notice) == expected, 'Embedded ART notice changed')
    for name, expected in staged['resources'].items():
        require(digest(resources / 'root' / name) == expected, 'Embedded ART data changed')
