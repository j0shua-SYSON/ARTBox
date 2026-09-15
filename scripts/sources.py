"""Fetch pinned source archives or exact file sets with gh into the build cache."""
import sys

sys.dont_write_bytecode = True

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import tarfile
import tempfile
from urllib.parse import quote

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


def source_archive(spec, cache):
    """Fetch one pinned archive into the configured download cache."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", spec["archive"]):
        raise RuntimeError("Source archive name must be a cache filename")
    downloads = cache / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / spec["archive"]
    partial = archive.with_suffix(archive.suffix + ".part")
    if archive.is_symlink() or partial.is_symlink():
        raise RuntimeError("Source archive cache files must not be symlinks")
    if not archive.is_file():
        with partial.open("wb") as output:
            subprocess.run(["gh", "api", f"repos/{spec['repository']}/tarball/{spec['commit']}"],
                           stdout=output, check=True)
        verify_archive(partial, spec)
        partial.replace(archive)
    verify_archive(archive, spec)
    return archive


def unpack_selected(archive, stage, selected):
    """Stream only validated pinned files into a temporary installation tree."""
    seen, prefix = set(), None
    with tarfile.open(archive, "r|gz") as bundle:
        for member in bundle:
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
            key = name.casefold()
            if key not in selected:
                continue
            entry = selected[key]
            if key in seen or name != entry["path"] or not member.isfile():
                raise RuntimeError("Duplicate, aliased or non-regular selected source")
            if member.size != entry["bytes"]:
                raise RuntimeError("Selected archive file size differs from the pin")
            seen.add(key)
            output = stage / entry["path"]
            output.parent.mkdir(parents=True, exist_ok=True)
            with bundle.extractfile(member) as source, output.open("wb") as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
    if seen != set(selected):
        raise RuntimeError("Source archive is missing selected files")


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


def obtain_files(name, spec, cache):
    """Install a complete hash-checked file selection; recheck all cached files."""
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", name) or not re.fullmatch(r"[0-9a-f]{40}", spec["commit"]):
        raise RuntimeError("Invalid pinned source name or commit")
    selected = {}
    for entry in spec["files"]:
        raw = entry["path"]
        path = PurePosixPath(raw)
        if path.is_absolute() or not path.parts or ".." in path.parts or ":" in raw or "\\" in raw or \
                path.as_posix() != raw or raw.casefold() == ".artbox-source.json":
            raise RuntimeError("Unsafe pinned source file path")
        key = raw.casefold()
        if key in selected or entry["bytes"] < 0 or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise RuntimeError("Duplicate or invalid pinned source file")
        if "git_blob" in entry and not re.fullmatch(r"[0-9a-f]{40}", entry["git_blob"]):
            raise RuntimeError("Invalid pinned Git blob")
        selected[key] = entry
    if not selected:
        raise RuntimeError("Empty pinned source selection")
    for key in selected:
        if any(parent.as_posix() in selected for parent in PurePosixPath(key).parents):
            raise RuntimeError("Pinned source file occupies a directory path")
    notice = selected.get(spec["notice"].casefold())
    if not notice or notice["path"] != spec["notice"] or notice["sha256"] != spec["notice_sha256"]:
        raise RuntimeError("Pinned source selection must include its reviewed notice")

    def verify(directory):
        for entry in selected.values():
            path = directory / entry["path"]
            if not path.resolve().is_relative_to(directory.resolve()) or not path.is_file():
                raise RuntimeError("Cached source file is missing or escapes its tree")
            if path.stat().st_size != entry["bytes"]:
                raise RuntimeError("Source file size differs from the pin")
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                raise RuntimeError("Source file SHA-256 differs from the pin")

    parent = cache / "sources"
    parent.mkdir(parents=True, exist_ok=True)
    installed = parent / f"{name}-{spec['commit'][:12]}"
    marker = installed / ".artbox-source.json"
    if installed.is_symlink():
        raise RuntimeError("Cached source directory must not be a symlink")
    if installed.exists():
        if not marker.is_file() or json.loads(marker.read_text(encoding="utf-8")) != spec:
            raise RuntimeError("Cached source provenance differs; use a fresh source cache")
        verify(installed)
    else:
        with tempfile.TemporaryDirectory(prefix=f"{name}-", dir=parent) as temporary:
            stage = Path(temporary) / "source"
            if "archive" in spec:
                unpack_selected(source_archive(spec, cache), stage, selected)
            else:
                for entry in selected.values():
                    path = stage / entry["path"]
                    path.parent.mkdir(parents=True, exist_ok=True)
                    if "git_blob" in entry:
                        endpoint = f"repos/{spec['repository']}/git/blobs/{entry['git_blob']}"
                        blob = json.loads(subprocess.check_output(["gh", "api", endpoint]))
                        if blob.get("encoding") != "base64" or blob.get("size") != entry["bytes"] or \
                                blob.get("sha") != entry["git_blob"]:
                            raise RuntimeError("Git blob metadata differs from the pin")
                        try:
                            data = base64.b64decode("".join(blob["content"].split()), validate=True)
                        except ValueError as error:
                            raise RuntimeError("Invalid Git blob encoding") from error
                        identity = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
                        if len(data) != entry["bytes"] or identity != entry["git_blob"]:
                            raise RuntimeError("Git blob content differs from the pin")
                        path.write_bytes(data)
                        continue
                    endpoint = f"repos/{spec['repository']}/contents/{quote(entry['path'], safe='/')}?ref={spec['commit']}"
                    with path.open("wb") as output:
                        subprocess.run(["gh", "api", endpoint, "-H", "Accept: application/vnd.github.raw+json"],
                                       stdout=output, check=True)
            verify(stage)
            (stage / ".artbox-source.json").write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
            stage.replace(installed)
    return installed


def obtain(name):
    specs = json.loads((ROOT / "third_party/sources.json").read_text(encoding="utf-8"))
    if name not in specs:
        raise RuntimeError(f"Unknown pinned source: {name}")
    spec = specs[name]
    cache = Path(os.environ["ARTBOX_CACHE_DIR"])
    if "files" in spec:
        return obtain_files(name, spec, cache)
    archive = source_archive(spec, cache)
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
