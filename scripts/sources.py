"""Fetch pinned source archives with gh into the configured build cache."""
import sys

sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import subprocess
import tarfile
import tempfile

from environment import ROOT, environment


def verify_archive(path, spec):
    if path.stat().st_size != spec["archive_bytes"]:
        raise RuntimeError("Source archive size differs from the pin")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != spec["archive_sha256"]:
        raise RuntimeError("Source archive SHA-256 differs from the pin")


def unpack(archive, destination, exclude):
    """Validate the complete extraction plan, then materialize regular files."""
    if destination.is_symlink() or (destination.exists() and any(destination.iterdir())):
        raise RuntimeError("Source extraction requires an empty directory")
    with tarfile.open(archive) as bundle:
        entries, names, prefix, total = {}, set(), None, 0
        for member in bundle.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or not path.parts or ".." in path.parts or ":" in member.name or "\\" in member.name:
                raise RuntimeError("Unsafe source archive path")
            prefix = prefix or path.parts[0]
            if path.parts[0] != prefix:
                raise RuntimeError("Source archive has multiple roots")
            if len(path.parts) == 1:
                if not member.isdir():
                    raise RuntimeError("Source archive root is not a directory")
                continue
            name = PurePosixPath(*path.parts[1:]).as_posix()
            if any(name == value or name.startswith(value + "/") for value in exclude):
                continue
            if name.casefold() in names:
                raise RuntimeError("Duplicate source archive path")
            names.add(name.casefold())
            if not (member.isfile() or member.isdir() or member.issym()):
                raise RuntimeError("Unsupported source archive member type")
            total += member.size
            if member.size < 0 or total > 512 * 1024 * 1024 or len(names) > 100000:
                raise RuntimeError("Source archive exceeds extraction bounds")
            entries[name] = member
        copies = {}
        for name, member in entries.items():
            for parent in PurePosixPath(name).parents:
                ancestor = entries.get(parent.as_posix())
                if ancestor is not None and not ancestor.isdir():
                    raise RuntimeError("Source file occupies a directory path")
            current, seen = name, set()
            while member.issym():
                if current in seen or len(seen) >= 32:
                    raise RuntimeError("Source alias cycle")
                seen.add(current)
                target = member.linkname
                if PurePosixPath(target).is_absolute() or ":" in target or "\\" in target:
                    raise RuntimeError("Unsafe source alias")
                current = posixpath.normpath(posixpath.join(posixpath.dirname(current), target))
                if current == ".." or current.startswith("../") or current not in entries:
                    raise RuntimeError("Source alias escapes the extracted tree")
                member = entries[current]
                if member.isdir():
                    raise RuntimeError("Directory aliases require an explicit source selection")
            if member.isfile():
                copies[name] = member
        destination.mkdir(parents=True, exist_ok=True)
        for name, member in copies.items():
            output = destination.joinpath(*PurePosixPath(name).parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            with bundle.extractfile(member) as source, output.open("wb") as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
            if os.name != "nt":
                output.chmod(member.mode & 0o777)


def obtain(name):
    specs = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))
    if name not in specs:
        raise RuntimeError(f"Unknown pinned source: {name}")
    spec = specs[name]
    cache = Path(os.environ["ARTBOX_CACHE_DIR"])
    downloads = cache / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / spec["archive"]
    if not archive.is_file():
        partial = archive.with_suffix(archive.suffix + ".part")
        with partial.open("wb") as output:
            subprocess.run(["gh", "api", f"repos/{spec['repository']}/tarball/{spec['commit']}"],
                           stdout=output, check=True)
        verify_archive(partial, spec)
        partial.replace(archive)
    verify_archive(archive, spec)
    parent = cache / "sources"
    parent.mkdir(parents=True, exist_ok=True)
    installed = parent / f"{name}-{spec['commit'][:12]}"
    marker = installed / ".artbox-source.json"
    if installed.exists():
        if not marker.is_file() or json.loads(marker.read_text()) != spec:
            raise RuntimeError("Cached source provenance differs; use a fresh source cache")
    else:
        with tempfile.TemporaryDirectory(prefix=f"{name}-", dir=parent) as temporary:
            stage = Path(temporary) / "source"
            unpack(archive, stage, spec["exclude"])
            (stage / ".artbox-source.json").write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
            stage.replace(installed)
    if hashlib.sha256((installed / spec["notice"]).read_bytes()).hexdigest() != spec["notice_sha256"]:
        raise RuntimeError("Source notice differs from the reviewed pin")
    return installed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    args = parser.parse_args()
    os.environ.update(environment())
    print(obtain(args.name))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        print(f"ARTBox sources: {error}", file=sys.stderr)
        sys.exit(1)
