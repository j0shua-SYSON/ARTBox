"""Opt-in installation of a pinned portable JDK into the configured build cache."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from environment import ROOT


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def extract(archive, destination, root):
    """Stream files into an empty tree; materialize checked internal file aliases."""
    if destination.is_symlink() or destination.exists() and any(destination.iterdir()):
        raise RuntimeError('JDK extraction requires an empty directory')
    destination.mkdir(parents=True, exist_ok=True)
    seen, links, total = set(), {}, 0
    def entry(name, kind, mode, size, source=None, target=None):
        nonlocal total
        parts = PurePosixPath(name)
        if parts.is_absolute() or '..' in parts.parts or ':' in name or '\\' in name or \
                not parts.parts or parts.parts[0] != root:
            raise RuntimeError('Unsafe JDK archive path')
        relative = PurePosixPath(*parts.parts[1:]).as_posix()
        if relative == '.':
            if kind != 'directory':
                raise RuntimeError('JDK root is not a directory')
            return
        if relative.casefold() in seen:
            raise RuntimeError('Duplicate JDK archive path')
        seen.add(relative.casefold())
        total += size
        if size < 0 or total > 1024**3 or len(seen) > 100000:
            raise RuntimeError('JDK extraction exceeds bounds')
        output = destination / relative
        if kind == 'directory':
            output.mkdir(parents=True, exist_ok=True)
        elif kind == 'file':
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open('wb') as stream:
                shutil.copyfileobj(source, stream, 1024 * 1024)
            if output.stat().st_size != size:
                raise RuntimeError('Truncated JDK archive file')
            if os.name != 'nt':
                output.chmod(mode & 0o777)
        elif kind in ('link', 'hardlink'):
            if not target or PurePosixPath(target).is_absolute() or ':' in target or '\\' in target:
                raise RuntimeError('Unsafe JDK alias')
            if kind == 'hardlink':
                path = PurePosixPath(target)
                if not path.parts or path.parts[0] != root:
                    raise RuntimeError('JDK hard link escapes its root')
                target = PurePosixPath(*path.parts[1:]).as_posix()
            else:
                target = posixpath.join(posixpath.dirname(relative), target)
            target = posixpath.normpath(target)
            if target == '..' or target.startswith('../'):
                raise RuntimeError('JDK alias escapes its root')
            links[relative] = target
        else:
            raise RuntimeError('Unsupported JDK archive entry')
    if archive.name.endswith('.zip'):
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                mode = member.external_attr >> 16
                if member.is_dir():
                    entry(member.filename, 'directory', mode, 0)
                elif stat.S_ISLNK(mode):
                    entry(member.filename, 'link', mode, 0, target=bundle.read(member).decode('utf-8'))
                else:
                    with bundle.open(member) as source:
                        entry(member.filename, 'file', mode, member.file_size, source)
    else:
        with tarfile.open(archive, 'r|gz') as bundle:
            for member in bundle:
                kind = ('file' if member.isfile() else 'directory' if member.isdir() else
                        'link' if member.issym() else 'hardlink' if member.islnk() else 'unsupported')
                if kind == 'file':
                    with bundle.extractfile(member) as source:
                        entry(member.name, kind, member.mode, member.size, source)
                else:
                    entry(member.name, kind, member.mode, member.size, target=member.linkname)
    for name, target in links.items():
        visited = {name}
        while target in links:
            if target in visited or len(visited) >= 32:
                raise RuntimeError('JDK alias cycle')
            visited.add(target)
            target = links[target]
        source, output = destination / target, destination / name
        if not source.is_file() or output.exists():
            raise RuntimeError('JDK alias is not an internal file')
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)


def obtain():
    manifest = json.loads((ROOT / 'third_party/jdk.json').read_text(encoding='utf-8'))
    machine = platform.machine().lower()
    machine = {'amd64': 'x64', 'x86_64': 'x64', 'arm64': 'aarch64'}.get(machine, machine)
    key = sys.platform + '-' + machine
    spec = manifest['archives'].get(key)
    if spec is None:
        raise RuntimeError('No pinned JDK for this platform; supply --java-home')
    cache = Path(os.environ['ARTBOX_CACHE_DIR'])
    downloads = cache / 'downloads/classlib-jdk'
    installed = cache / 'toolchains' / ('temurin-' + manifest['version'] + '-' + key)
    marker = installed / 'artbox-jdk.json'
    if installed.is_symlink():
        raise RuntimeError('JDK installation must not be an alias')
    if installed.is_dir():
        record = json.loads(marker.read_text(encoding='utf-8'))
        if record['archive'] != spec:
            raise RuntimeError('Installed JDK provenance differs')
        for name, expected in record['files'].items():
            path = installed / name
            if path.is_symlink() or digest(path) != expected:
                raise RuntimeError('Installed JDK file changed: ' + name)
        return installed / spec['home'], spec
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / spec['name']
    if archive.is_symlink():
        raise RuntimeError('JDK download must not be an alias')
    def verify(path):
        if path.stat().st_size != spec['bytes'] or digest(path) != spec['sha256']:
            raise RuntimeError('JDK archive differs from the reviewed checksum')
    if not archive.is_file():
        with tempfile.TemporaryDirectory(prefix='jdk-download-', dir=downloads) as stage:
            print('Downloading pinned portable JDK 17 (' + key + ')', flush=True)
            subprocess.run(['gh', 'release', 'download', manifest['tag'], '--repo', manifest['repository'],
                            '--pattern', spec['name'], '--dir', stage], check=True)
            partial = Path(stage) / spec['name']
            verify(partial)
            partial.replace(archive)
    verify(archive)
    installed.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='jdk-install-', dir=installed.parent) as stage:
        destination = Path(stage) / 'jdk'
        extract(archive, destination, spec['root'])
        home = destination / spec['home']
        if not (home / 'bin' / ('java.exe' if os.name == 'nt' else 'java')).is_file():
            raise RuntimeError('Portable JDK archive is missing java')
        record = {'repository': manifest['repository'], 'tag': manifest['tag'], 'archive': spec,
                  'files': {p.relative_to(destination).as_posix(): digest(p)
                            for p in sorted(destination.rglob('*')) if p.is_file()}}
        (destination / 'artbox-jdk.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        destination.replace(installed)
    return installed / spec['home'], spec
