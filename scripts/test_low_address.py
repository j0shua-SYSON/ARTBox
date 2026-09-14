"""Measure the native low-address window and verify signed Apple probe layouts."""
import sys
sys.dont_write_bytecode = True

import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import struct
import subprocess

from environment import ROOT, environment


def run(*args):
    result = subprocess.run([str(a) for a in args], capture_output=True, check=True)
    return result.stdout


def macho(path, target, guard):
    data = path.read_bytes()
    magic, cpu, _, kind, count, size, flags, _ = struct.unpack_from('<8I', data)
    if magic != 0xfeedfacf or kind != 2 or not flags & 0x200000:
        raise RuntimeError('Expected a 64-bit PIE Mach-O executable')
    if cpu not in (0x100000c, 0x1000007) or (target == 2 and cpu != 0x100000c):
        raise RuntimeError('Unexpected probe CPU')
    cursor, segments, platforms, signatures = 32, [], [], []
    for _ in range(count):
        command, length = struct.unpack_from('<II', data, cursor)
        if length < 8 or cursor + length > 32 + size or cursor + length > len(data):
            raise RuntimeError('Malformed load command')
        if command == 0x19:
            name, address, span, _, _, maximum, initial = struct.unpack_from('<16sQQQQII', data, cursor + 8)
            if maximum & 6 == 6 or initial & 6 == 6:
                raise RuntimeError('Probe has writable executable memory')
            segments.append({'name': name.rstrip(b'\0').decode(), 'address': address,
                             'bytes': span, 'maximum': maximum, 'initial': initial})
        if command == 0x32:
            platforms.append(struct.unpack_from('<II', data, cursor + 8))
        if command == 0x1d:
            start, amount = struct.unpack_from('<II', data, cursor + 8)
            if not amount or start + amount > len(data):
                raise RuntimeError('Invalid code signature span')
            signatures.append(start)
        cursor += length
    zero = [s for s in segments if s['name'] == '__PAGEZERO']
    if cursor != 32 + size or len(zero) != 1 or len(signatures) != 1:
        raise RuntimeError('Missing guard or signature')
    if zero[0] != {'name': '__PAGEZERO', 'address': 0, 'bytes': guard, 'maximum': 0, 'initial': 0}:
        raise RuntimeError('Linker did not preserve the requested null guard')
    if len(platforms) != 1 or platforms[0][0] != target or (target == 2 and platforms[0][1] != 15 << 16):
        raise RuntimeError('Unexpected probe platform or deployment target')
    entitlements = run('codesign', '-d', '--entitlements', ':-', path)
    if plistlib.loads(entitlements) != {}:
        raise RuntimeError('Probe contains entitlements')
    run('codesign', '--verify', '--strict', path)
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'cpu': cpu, 'platform': target, 'minos': platforms[0][1],
            'segments': segments, 'entitlements': {}, 'signature_verified': True}


def observe(executable, available):
    result = json.loads(run(executable, 'available' if available else 'blocked'))
    if result.get('success') is not True or result.get('cleanup_ok') is not True:
        raise RuntimeError('Native low-address contract failed')
    if result['low_window_available'] is not available or result['reference_roundtrip'] is not available:
        raise RuntimeError('Native result contradicts the expected address window')
    return result


def main():
    os.environ.update(environment())
    build = Path(os.environ['ARTBOX_BUILD_DIR'])
    artifacts = Path(os.environ['ARTBOX_ARTIFACTS_DIR'])
    source = ROOT / 'tests/native_low_address.c'
    default = build / 'host/test_low_address'
    if os.name == 'nt':
        default = default.with_suffix('.exe')
        if not default.is_file():
            default = default.parent / 'Release' / default.name
    if not default.is_file():
        raise RuntimeError('Build the host tests first')
    result = {'scope': 'Native address-space probe; no ART or device execution claim',
              'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'system': platform.system(), 'architecture': platform.machine()}
    if sys.platform != 'darwin':
        result['native'] = observe(default, True)
    else:
        folder = build / 'm3/low-address'
        folder.mkdir(parents=True, exist_ok=True)
        result['profiles'] = {}
        for name, guard in [('default', 1 << 32), ('reduced', 1 << 16)]:
            host = default if name == 'default' else build / 'host/test_low_address_reduced_guard'
            run('codesign', '--force', '--sign', '-', '--entitlements', ROOT / 'app/ARTBox.entitlements', host)
            host_layout = macho(host, 1, guard)
            observed = observe(host, name == 'reduced')
            device = folder / ('probe-' + name + '-ios')
            command = ['xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios15.0',
                       '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', str(source), '-o', str(device)]
            if name == 'reduced':
                command.append('-Wl,-pagezero_size,0x10000')
            run(*command)
            run('codesign', '--force', '--sign', '-', '--entitlements', ROOT / 'app/ARTBox.entitlements', device)
            result['profiles'][name] = {'host': host_layout, 'native': observed,
                                        'ios': macho(device, 2, guard)}
        result['device_execution_verified'] = False
    (artifacts / 'm3-low-address.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, struct.error, subprocess.CalledProcessError) as error:
        print(f'ARTBox low-address probe: {error}', file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr.decode(errors='replace'), file=sys.stderr)
        sys.exit(1)
