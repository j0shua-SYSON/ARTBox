"""Record native Binder reference availability without installing or loading a kernel."""
import sys
sys.dont_write_bytecode = True

import argparse
import gzip
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import struct
import subprocess

from environment import environment


def probe():
    record = {"scope": "Read-only native Binder capability inventory, not M4 acceptance",
              "platform": sys.platform, "kernel": platform.release(),
              "available": False, "devices": [], "modules": [], "configuration": {}, "errors": []}
    if sys.platform != "linux":
        record["reason"] = "A real Linux kernel is required for the reference"
        return record
    import fcntl

    # Discover only Binder's conventional locations. Do not open binder-control,
    # create a context manager, load modules or modify an existing device.
    paths = {Path('/dev/binder'), Path('/dev/hwbinder'), Path('/dev/vndbinder')}
    binderfs = Path('/dev/binderfs')
    try:
        if binderfs.is_dir():
            paths.update(p for p in binderfs.iterdir() if p.name != 'binder-control')
    except OSError as error:
        record['errors'].append(str(error))
    for path in sorted(paths):
        try:
            if not path.exists() or not stat.S_ISCHR(path.stat().st_mode):
                continue
            entry = {'path': str(path)}
            record['devices'].append(entry)
            fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
            try:
                version = bytearray(4)
                fcntl.ioctl(fd, 0xc0046209, version, True)  # ARM64/x86_64 BINDER_VERSION
                entry['protocol'] = struct.unpack('<i', version)[0]
                record['available'] |= entry['protocol'] == 8
            finally:
                os.close(fd)
        except OSError as error:
            record['errors'].append(f'{path}: {error}')

    module_root = Path('/lib/modules') / platform.release()
    try:
        if module_root.is_dir():
            # Newer kernels split binder_linux into binder, binder_alloc and
            # binderfs. Record both layouts, including compressed modules.
            record['modules'] = sorted(str(p) for p in module_root.rglob('binder*.ko*') if p.is_file())
    except OSError as error:
        record['errors'].append(str(error))
    config_paths = [Path('/proc/config.gz'), Path('/boot') / ('config-' + platform.release())]
    for path in config_paths:
        try:
            if not path.is_file():
                continue
            data = gzip.decompress(path.read_bytes()) if path.suffix == '.gz' else path.read_bytes()
            record['configuration'][str(path)] = [line for line in data.decode().splitlines()
                                                  if 'CONFIG_ANDROID_BINDER' in line]
        except (OSError, UnicodeError) as error:
            record['errors'].append(f'{path}: {error}')
    if not record['available']:
        record['reason'] = 'No accessible protocol-8 Binder device; no kernel semantic comparison ran'
    # Hosted images can enable Binder in their kernel while omitting the module
    # package. Inspect their existing authenticated package index. No update,
    # download, installation, module load or system cache write happens here.
    apt = shutil.which('apt-cache')
    if not record['modules'] and apt:
        record['package_candidates'] = []
        wanted = {'Package', 'Version', 'Architecture', 'Size', 'SHA256', 'Filename'}
        for prefix in ('linux-modules-', 'linux-modules-extra-'):
            package = prefix + platform.release()
            result = subprocess.run([apt, '-o', 'Dir::Cache::pkgcache=', '-o', 'Dir::Cache::srcpkgcache=',
                                     'show', '--no-all-versions', package], capture_output=True, text=True, timeout=30)
            selected = {}
            for line in result.stdout.splitlines():
                key, sep, value = line.partition(': ')
                if sep and key in wanted:
                    selected[key] = value
            record['package_candidates'].append({'query': package, 'exit': result.returncode, 'metadata': selected})
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-device', action='store_true', help='Fail if no usable native Binder device exists')
    args = parser.parse_args()
    os.environ.update(environment())
    record = probe()
    output = Path(os.environ['ARTBOX_ARTIFACTS_DIR']) / 'm4-binder-capabilities.json'
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(record, indent=2))
    return 1 if args.require_device and not record['available'] else 0


if __name__ == '__main__':
    sys.exit(main())
