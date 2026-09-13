"""Link and sign the paired M1 framework inputs with public Apple build tools."""

import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import time

from environment import ROOT


def command(*args, capture=False):
    result = subprocess.run([str(value) for value in args], check=True, capture_output=capture)
    return result.stdout if capture else b""


def prepare(fixture, directory, platform):
    if sys.platform != "darwin":
        raise RuntimeError("Linking and signing Apple frameworks requires macOS with Xcode")
    packed = directory / "pack"
    command(sys.executable, "-B", ROOT / "tools/pack_elf.py", fixture, packed, platform)
    info = json.loads((packed / "pack-info.json").read_text())
    sdk = "iphoneos" if platform == "ios" else "macosx"
    target = "arm64-apple-ios15.0" if platform == "ios" else "arm64-apple-macos11.0"
    sysroot = command("xcrun", "--sdk", sdk, "--show-sdk-path", capture=True).decode().strip()
    results = {}
    for kind in ("Converted", "Wrapped"):
        name = f"ARTBox{kind}"
        framework = directory / f"{name}.framework"
        framework.mkdir(parents=True, exist_ok=True)
        binary = framework / name
        started = time.perf_counter_ns()
        if kind == "Converted":
            shutil.copyfile(packed / "converted.dylib", binary)
        else:
            command("xcrun", "--sdk", sdk, "clang", "-target", target, "-isysroot", sysroot,
                    "-dynamiclib", "-I", packed, packed / "wrapper.S",
                    f"-Wl,-install_name,@rpath/{name}.framework/{name}",
                    "-Wl,-exported_symbol,_artbox_guest_start", "-o", binary)
        unsigned_bytes = binary.stat().st_size
        binary.chmod(0o755)
        plist = {
            "CFBundleIdentifier": f"com.j0shua.{name}", "CFBundleExecutable": name,
            "CFBundleName": name, "CFBundlePackageType": "FMWK", "CFBundleVersion": "1",
            "CFBundleShortVersionString": "1.0", "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleSupportedPlatforms": ["iPhoneOS" if platform == "ios" else "MacOSX"],
        }
        plist["MinimumOSVersion" if platform == "ios" else "LSMinimumSystemVersion"] = "15.0" if platform == "ios" else "11.0"
        (framework / "Info.plist").write_bytes(plistlib.dumps(plist))
        command("codesign", "--force", "--sign", "-", "--timestamp=none", framework)
        signing_ns = time.perf_counter_ns() - started
        command("codesign", "--verify", "--strict", "--verbose=2", framework)
        load_commands = command("xcrun", "otool", "-l", binary, capture=True)
        for segment in re.split(rb"Load command \d+", load_commands):
            if b"LC_SEGMENT_64" not in segment:
                continue
            permissions = re.search(rb"initprot\s+(0x[0-9a-fA-F]+)", segment)
            if not permissions or int(permissions.group(1), 16) & 6 == 6:
                raise RuntimeError("Unverified or writable/executable framework segment")
        (directory / f"{name}-load-commands.txt").write_bytes(load_commands)
        results[kind.lower()] = {
            "binary": str(binary.resolve()), "unsigned_bytes": unsigned_bytes,
            "signed_bytes": binary.stat().st_size, "link_or_copy_and_sign_ns": signing_ns,
            "signed_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        }
    toolchain = json.loads(fixture.with_name("elf-build.json").read_text())
    if toolchain["elf_sha256"] != info["input_sha256"]:
        raise RuntimeError("ELF differs from its toolchain provenance record")
    return {"pack": info, "toolchain": toolchain, "frameworks": results}
