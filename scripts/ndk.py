"""Obtain the pinned, relocatable NDK used only to build the M1 fixture."""

import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tempfile
import time
import urllib.request
import zipfile

REVISION = "28.2.13676358"
NAME = "android-ndk-r28c"
# Sizes and SHA-1 values published in Google's repository2-3.xml. HTTPS plus
# the pinned digest verifies download identity; record SHA-256 after retrieval.
ARCHIVES = {
    "win32": ("windows", 748118221, "086bba43ff2f5eb0e387b15c8278bb4e0d89ba1d"),
    "darwin": ("darwin", 952495160, "fc20a6bf15a30fb3428c9b60a7308793a362dc6d"),
    "linux": ("linux", 722261334, "a7b54a5de87fecd125a17d54f73c446199e72a64"),
}


def checked_root(path):
    properties = (path / "source.properties").read_text(encoding="utf-8")
    if f"Pkg.Revision = {REVISION}" not in properties:
        raise RuntimeError(f"M1 requires NDK r28c ({REVISION})")
    return path


def extract(archive, destination):
    """Extract regular files before symlinks, preserving relocatable Unix tools."""
    links = []
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            relative = PurePosixPath(member.filename)
            if relative.is_absolute() or ".." in relative.parts or "\\" in member.filename or ":" in member.filename:
                raise RuntimeError("NDK archive contains an unsafe path")
            if not relative.parts or relative.parts[0] != NAME:
                raise RuntimeError("Unexpected NDK archive root")
            path = destination.joinpath(*relative.parts)
            mode = member.external_attr >> 16
            if member.is_dir():
                path.mkdir(parents=True, exist_ok=True)
            elif stat.S_ISLNK(mode):
                target = bundle.read(member).decode("utf-8")
                resolved = (path.parent / target).resolve()
                if Path(target).is_absolute() or not resolved.is_relative_to(destination.resolve()):
                    raise RuntimeError("NDK symlink escapes extraction root")
                links.append((path, target))
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(member) as source, path.open("wb") as output:
                    shutil.copyfileobj(source, output, 1024 * 1024)
                if os.name != "nt" and mode:
                    path.chmod(mode & 0o777)
    for path, target in links:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)


def obtain(explicit=None):
    configured = explicit or os.environ.get("ARTBOX_NDK_ROOT")
    if configured:
        return checked_root(Path(configured).expanduser().resolve())
    cache = Path(os.environ["ARTBOX_CACHE_DIR"])
    installed = cache / "toolchains" / NAME
    if installed.is_dir():
        return checked_root(installed)
    if sys.platform not in ARCHIVES:
        raise RuntimeError("Supply ARTBOX_NDK_ROOT for this build host")
    host, expected_size, expected_sha1 = ARCHIVES[sys.platform]
    downloads = cache / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / f"{NAME}-{host}.zip"
    if not archive.is_file():
        partial = archive.with_suffix(".zip.part")
        url = f"https://dl.google.com/android/repository/{archive.name}"
        print(f"Downloading pinned NDK r28c ({expected_size / 1e6:.0f} MB)", flush=True)
        with urllib.request.urlopen(url, timeout=60) as source, partial.open("wb") as output:
            copied, last_report = 0, time.monotonic()
            while True:
                block = source.read(1024 * 1024)
                if not block:
                    break
                output.write(block)
                copied += len(block)
                if time.monotonic() - last_report >= 10:
                    print(f"NDK download: {copied / 1e6:.0f} MB", flush=True)
                    last_report = time.monotonic()
        partial.replace(archive)
    sha1, sha256 = hashlib.sha1(), hashlib.sha256()
    with archive.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            sha1.update(block)
            sha256.update(block)
    if archive.stat().st_size != expected_size or sha1.hexdigest() != expected_sha1:
        raise RuntimeError(f"Pinned NDK archive verification failed: {archive.name}")
    archive.with_suffix(".zip.sha256").write_text(sha256.hexdigest() + "\n", encoding="ascii")
    installed.parent.mkdir(parents=True, exist_ok=True)
    print("Extracting the verified NDK archive", flush=True)
    with tempfile.TemporaryDirectory(prefix="ndk-", dir=installed.parent) as stage:
        extract(archive, Path(stage))
        checked_root(Path(stage) / NAME).replace(installed)
    return installed
