"""Link and verify the controlled ELF wrapper as a signed Apple framework."""
import sys
sys.dont_write_bytecode = True

import json
import hashlib
from pathlib import Path
import plistlib
import time

from environment import ROOT
from guest_bundle import command

sys.path.insert(0, str(ROOT / "tools"))
from wrap_dynamic import verify_macho


def prepare(packed, directory, platform, notice, notice_sha256, extra_notices=None):
    if sys.platform != "darwin" or platform not in ("macos", "ios"):
        raise RuntimeError("Dynamic framework linking requires Xcode on macOS")
    name = "ARTBoxBionicSlice"
    framework = directory / (name + ".framework")
    framework.mkdir(parents=True, exist_ok=True)
    binary = framework / name
    sdk = "iphoneos" if platform == "ios" else "macosx"
    target = "arm64-apple-ios15.0" if platform == "ios" else "arm64-apple-macos11.0"
    sysroot = command("xcrun", "--sdk", sdk, "--show-sdk-path", capture=True).decode().strip()
    layout = json.loads((packed / "layout.json").read_text(encoding="utf-8"))
    started = time.perf_counter_ns()
    command("xcrun", "--sdk", sdk, "clang", "-target", target, "-isysroot", sysroot, "-dynamiclib",
            "-I", packed, packed / "wrapper.S", f"-Wl,-install_name,@rpath/{name}.framework/{name}",
            "-Wl,-exported_symbol,_artbox_dynamic_rx", "-Wl,-exported_symbol,_artbox_dynamic_rw", "-o", binary)
    before = verify_macho(binary.read_bytes(), layout)
    unsigned_size = binary.stat().st_size
    plist = {"CFBundleIdentifier": "org.artbox.BionicSlice", "CFBundleExecutable": name,
             "CFBundleName": name, "CFBundlePackageType": "FMWK", "CFBundleVersion": "1",
             "CFBundleShortVersionString": "1.0", "CFBundleInfoDictionaryVersion": "6.0",
             "CFBundleSupportedPlatforms": ["iPhoneOS" if platform == "ios" else "MacOSX"]}
    plist["MinimumOSVersion" if platform == "ios" else "LSMinimumSystemVersion"] = "15.0" if platform == "ios" else "11.0"
    (framework / "Info.plist").write_bytes(plistlib.dumps(plist))
    notice_data = notice.read_bytes()
    if hashlib.sha256(notice_data).hexdigest() != notice_sha256:
        raise RuntimeError("Bionic framework notice differs from its reviewed source pin")
    (framework / "BIONIC-NOTICE.txt").write_bytes(notice_data)
    notice_hashes = {"BIONIC-NOTICE.txt": notice_sha256}
    for name, (path, expected) in (extra_notices or {}).items():
        if Path(name).name != name or name == "BIONIC-NOTICE.txt":
            raise RuntimeError("Invalid additional framework notice name")
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f"Framework notice differs from its reviewed source: {name}")
        (framework / name).write_bytes(data)
        notice_hashes[name] = expected
    command("codesign", "--force", "--sign", "-", "--timestamp=none", framework)
    command("codesign", "--verify", "--strict", "--verbose=2", framework)
    after = verify_macho(binary.read_bytes(), layout)
    if any(before[key] != after[key] for key in ("load_bias", "rx_address", "rw_address")):
        raise RuntimeError("Signing changed the verified guest address layout")
    (directory / "load-commands.txt").write_bytes(command("xcrun", "otool", "-l", binary, capture=True))
    return binary, {"platform": platform, "target": target, "layout": after, "notice_sha256": notice_sha256,
                    "notices": notice_hashes,
                    "unsigned_bytes": unsigned_size,
                    "signed_bytes": binary.stat().st_size, "link_verify_sign_ns": time.perf_counter_ns() - started}
