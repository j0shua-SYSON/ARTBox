"""Build the pinned native Apple ANGLE Metal profile and preserve source evidence."""
# SPDX-License-Identifier: MIT
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile

from environment import ROOT, environment
from sources import obtain_files

CATALOG = ROOT / 'third_party/angle/sources.json'
PROFILE = ROOT / 'third_party/angle/metal-build.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def validate_profile(catalog, profile):
    """Fail closed on a changed compilation scope or an executable backend addition."""
    allowed = {entry['path'] for entry in catalog['angle']['files']}
    if profile.get('schema') != 1 or set(profile['platforms']) != {'macos', 'ios'}:
        raise RuntimeError('Unknown ANGLE build profile')
    units = profile['common'] + sum(profile['platforms'].values(), [])
    if len(units) != len(set(units)) or not set(units) <= allowed:
        raise RuntimeError('Duplicate or unpinned ANGLE compilation unit')
    grouped_units = {name for group in profile['original_groups'].values() for name in group
                     if Path(name).suffix in ('.c', '.cc', '.cpp', '.mm')}
    if grouped_units != set(profile['common']):
        raise RuntimeError('ANGLE compilation differs from its reviewed upstream groups')
    forbidden = ('/vulkan/', '/d3d/', '/wgpu/', '/renderer/gl/', '/renderer/null/',
                 '/renderer/cl/', '/tests/', '/samples/', '/SwiftShader/')
    if any(any(part in name for part in forbidden) for name in units):
        raise RuntimeError('Unexpected ANGLE backend or test implementation')
    if any(Path(name).suffix not in ('.c', '.cc', '.cpp', '.mm') for name in units):
        raise RuntimeError('Invalid ANGLE compilation unit')
    required = {'ANGLE_ENABLE_METAL', 'ANGLE_CAPTURE_ENABLED=0', 'ANGLE_STATIC=1', 'USE_SYSTEM_ZLIB'}
    if not required <= set(profile['defines']) or any(
            token in define for define in profile['defines'] for token in
            ('OWNERSHIP_IDENTITY', 'ANGLE_ENABLE_VULKAN', 'ANGLE_ENABLE_GL=', 'ANGLE_ENABLE_WGPU',
             'ANGLE_ENABLE_D3D', 'ANGLE_ENABLE_CL', 'ANGLE_ENABLE_NULL', 'ANGLE_USE_ABSEIL')):
        raise RuntimeError('Unexpected ANGLE rendering feature')
    for source, notices in profile['notices'].items():
        if not set(notices) <= {item['path'] for item in catalog[source]['files']}:
            raise RuntimeError('Unpinned ANGLE license')


def run(command, log, cwd=ROOT):
    with log.open('wb') as target:
        result = subprocess.run(list(map(str, command)), cwd=cwd, stdout=target, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'ANGLE command failed ({result.returncode}); see {log}')
    return log.read_text(encoding='utf-8')


def generated_headers(output, catalog, profile):
    """Isolate serialized program caches by the entire source and recipe identity."""
    output.mkdir(parents=True, exist_ok=True)
    cache_id = hashlib.sha256(json.dumps({'sources': catalog, 'profile': profile},
                                       sort_keys=True).encode()).hexdigest()
    commit = catalog['angle']['commit']
    (output / 'angle_commit.h').write_text(
        f'#define ANGLE_COMMIT_HASH "{commit}"\n#define ANGLE_COMMIT_HASH_SIZE 40\n'
        '#define ANGLE_COMMIT_DATE "pinned android-15.0.0_r1"\n#define ANGLE_COMMIT_POSITION 0\n',
        encoding='utf-8')
    (output / 'ANGLEShaderProgramVersion.h').write_text(
        f'#define ANGLE_PROGRAM_VERSION "{cache_id}"\n#define ANGLE_PROGRAM_VERSION_HASH_SIZE 32\n',
        encoding='utf-8')
    return {p.name: digest(p) for p in sorted(output.glob('*.h'))}


def cmake_project(source, generated, profile, platform):
    def quote(path):
        return '"' + str(path).replace('\\', '/').replace('"', '\\"') + '"'
    angle = source['angle']
    units = [angle / name for name in profile['common'] + profile['platforms'][platform]]
    units.append(source['chromium-compression'] / 'third_party/zlib/google/compression_utils_portable.cc')
    includes = [angle / name for name in profile['include_dirs']] + [generated,
                source['chromium-compression'] / 'third_party/zlib/google']
    frameworks = ['Foundation', 'Metal', 'QuartzCore', 'IOSurface', 'CoreGraphics']
    frameworks += ['Cocoa', 'IOKit'] if platform == 'macos' else ['UIKit']
    text = ('cmake_minimum_required(VERSION 3.24)\n'
            'project(ARTBoxANGLE LANGUAGES C CXX OBJCXX)\n'
            'set(CMAKE_CXX_STANDARD 17)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n'
            'add_library(ARTBoxANGLE SHARED\n' + '\n'.join(map(quote, units)) + ')\n'
            'set_target_properties(ARTBoxANGLE PROPERTIES FRAMEWORK TRUE FRAMEWORK_VERSION A\n'
            'MACOSX_FRAMEWORK_IDENTIFIER org.artbox.ANGLE\n'
            'XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)\n'
            'target_include_directories(ARTBoxANGLE PRIVATE ' + ' '.join(map(quote, includes)) + ')\n'
            'target_compile_definitions(ARTBoxANGLE PRIVATE ' + ' '.join(map(quote, profile['defines'])) + ')\n'
            'target_compile_options(ARTBoxANGLE PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fno-objc-arc>")\n'
            'target_link_libraries(ARTBoxANGLE PRIVATE z ' +
            ' '.join(quote('-framework ' + f) for f in frameworks) + ')\n')
    if platform == 'macos':
        text += ('add_executable(angle_probe ' + quote(ROOT / 'tests/native/angle_probe.mm') + ')\n'
            'target_include_directories(angle_probe PRIVATE ' + quote(angle / 'include') + ')\n'
            'target_compile_options(angle_probe PRIVATE -Wall -Wextra -Werror -fno-objc-arc)\n'
            'target_link_libraries(angle_probe PRIVATE ARTBoxANGLE "-framework Foundation" "-framework Metal")\n')
    return text


def validate_probe(record):
    if (record.get('schema') != 1 or record.get('translated_shaders') != 2 or
            record.get('invalid_shader_rejected') is not True or
            not isinstance(record.get('metal_available'), bool)):
        raise RuntimeError('ANGLE shader contract did not pass')
    if record['metal_available']:
        if (record.get('backend') != 'metal' or record.get('verified_pixels') != 2048 or
                record.get('surfaces_destroyed') != 1 or record.get('contexts_destroyed') != 1):
            raise RuntimeError('ANGLE pixel/lifetime contract did not pass')
    elif record.get('verified_pixels') != 0:
        raise RuntimeError('ANGLE reported pixels without a Metal device')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true', help='Verify sources without requiring Apple tools')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8: parser.error('--jobs must be between 1 and 8')
    os.environ.update(environment())
    catalog = json.loads(CATALOG.read_bytes()); profile = json.loads(PROFILE.read_bytes())
    validate_profile(catalog, profile)
    output = (args.build_dir or Path(os.environ['ARTBOX_BUILD_DIR']) / 'm5/angle').resolve()
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / 'build.json'
    if report_path.exists(): report_path.unlink()
    sources = {name: obtain_files(name, spec, Path(os.environ['ARTBOX_CACHE_DIR'])) for name, spec in catalog.items()}
    generated = output / 'generated'
    headers = generated_headers(generated, catalog, profile)
    project = ['LICENSE', 'THIRD_PARTY.md', 'docs/graphics.md', 'scripts/build_angle.py', 'scripts/sources.py',
        'scripts/environment.py', 'tests/test_angle_build.py', 'tests/native/angle_probe.mm',
        'third_party/angle/sources.json', 'third_party/angle/metal-build.json']
    entries = {'upstream/' + n + '/' + e['path']: sources[n] / e['path'] for n, s in catalog.items() for e in s['files']}
    entries.update({'artbox/' + n: ROOT / n for n in project})
    entries.update({'generated/' + n: generated / n for n in headers})
    archive = output / 'corresponding-source.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, path in sorted(entries.items()):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)); info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, path.read_bytes())
    record = dict(schema=1, sources=catalog, project_files={n: digest(ROOT/n) for n in project},
                  source_bundle_sha256=digest(archive), generated=headers, platforms={}, prepared_only=args.prepare)
    if not args.prepare:
        if sys.platform != 'darwin': raise RuntimeError('Native Apple compilation requires Xcode on macOS; use --prepare elsewhere')
        record['compiler'] = run(['xcrun', 'clang++', '--version'], output/'compiler.log').splitlines()[0]
        for platform, sdk, minimum in [('macos', 'macosx', '12.0'), ('ios', 'iphoneos', '15.0')]:
            directory = output/platform; directory.mkdir(exist_ok=True)
            (directory/'CMakeLists.txt').write_text(cmake_project(sources, generated, profile, platform), encoding='utf-8')
            binary = directory/'build'
            command = ['cmake', '-S', directory, '-B', binary, '-G', 'Ninja' if shutil.which('ninja') else 'Unix Makefiles',
                '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_SYSROOT='+sdk,
                '-DCMAKE_OSX_DEPLOYMENT_TARGET='+minimum, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
            if platform == 'ios': command.append('-DCMAKE_SYSTEM_NAME=iOS')
            run(command, directory/'configure.log')
            start = time.monotonic()
            run(['cmake', '--build', binary, '--parallel', args.jobs], directory/'compile.log')
            elapsed = time.monotonic()-start
            framework = binary/'ARTBoxANGLE.framework'; code = framework/'ARTBoxANGLE'
            notices = framework/'Resources'/'Notices' if platform == 'macos' else framework/'Notices'
            notices.mkdir(parents=True, exist_ok=True)
            for name, paths in profile['notices'].items():
                for path in paths:
                    shutil.copyfile(sources[name]/path, notices/(name+'-'+path.replace('/', '_')))
            run(['codesign', '--force', '--sign', '-', '--timestamp=none', framework], directory/'sign.log')
            run(['codesign', '--verify', '--strict', '--verbose=2', framework], directory/'verify.log')
            run(['xcrun', 'vtool', '-show-build', code], directory/'build-version.log')
            run(['xcrun', 'nm', '-u', code], directory/'imports.log')
            record['platforms'][platform] = dict(binary_sha256=digest(code), bytes=code.stat().st_size,
                compile_seconds=elapsed, translation_units=len(profile['common'])+len(profile['platforms'][platform])+1,
                minimum=minimum, notices={p.name: digest(p) for p in notices.iterdir()})
            if platform == 'macos':
                probe = binary/'angle_probe'
                run(['codesign', '--force', '--sign', '-', '--timestamp=none', probe], directory/'probe-sign.log')
                run([probe], directory/'probe.log')
                rows = [line for line in (directory/'probe.log').read_text(encoding='utf-8').splitlines() if line.startswith('{')]
                if len(rows) != 1: raise RuntimeError('ANGLE probe must emit exactly one JSON record')
                record['probe'] = json.loads(rows[0]); validate_probe(record['probe'])
                record['probe_binary_sha256'] = digest(probe)
                record['probe_output_sha256'] = digest(directory/'probe.log')
            save(report_path, record)
    save(report_path, record)
    print('ANGLE sources prepared' if args.prepare else 'ANGLE Mac/iOS build and native probe passed')
    print(report_path)


if __name__ == '__main__':
    try: main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print('ARTBox ANGLE: ' + str(error), file=sys.stderr); sys.exit(1)
