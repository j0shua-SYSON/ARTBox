"""Link and verify the controlled ELF wrapper as a signed Apple framework."""
import sys
sys.dont_write_bytecode = True

import json
import hashlib
import plistlib
import time

from environment import ROOT
from guest_bundle import command

sys.path.insert(0, str(ROOT / "tools"))
from wrap_dynamic import verify_macho


def prepare(packed, directory, platform, notice, notice_sha256, extra_notices=None, name="ARTBoxBionicSlice",
            notice_name="BIONIC-NOTICE.txt"):
    if sys.platform != "darwin" or platform not in ("macos", "ios"):
        raise RuntimeError("Dynamic framework linking requires Xcode on macOS")
    if not name.isascii() or not name.isalnum() or not name.startswith("ARTBox"):
        raise ValueError("Framework name must be an ASCII ARTBox identifier")
    def valid_notice(value):
        return value.isascii() and value.endswith('.txt') and all(c.isalnum() or c in '-_.' for c in value)
    if not valid_notice(notice_name) or any(not valid_notice(n) for n in (extra_notices or {})):
        raise ValueError("Framework notices must have plain ASCII .txt filenames")
    notice_names = [notice_name, *(extra_notices or {})]
    if len({n.casefold() for n in notice_names}) != len(notice_names):
        raise ValueError("Duplicate framework notice name")
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
    plist = {"CFBundleIdentifier": "org.artbox." + name[len("ARTBox"):], "CFBundleExecutable": name,
             "CFBundleName": name, "CFBundlePackageType": "FMWK", "CFBundleVersion": "1",
             "CFBundleShortVersionString": "1.0", "CFBundleInfoDictionaryVersion": "6.0",
             "CFBundleSupportedPlatforms": ["iPhoneOS" if platform == "ios" else "MacOSX"]}
    plist["MinimumOSVersion" if platform == "ios" else "LSMinimumSystemVersion"] = "15.0" if platform == "ios" else "11.0"
    (framework / "Info.plist").write_bytes(plistlib.dumps(plist))
    notice_data = notice.read_bytes()
    if hashlib.sha256(notice_data).hexdigest() != notice_sha256:
        raise RuntimeError("Framework notice differs from its reviewed source pin")
    (framework / notice_name).write_bytes(notice_data)
    notice_hashes = {notice_name: notice_sha256}
    for name, (path, expected) in (extra_notices or {}).items():
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f"Framework notice differs from its reviewed source: {name}")
        (framework / name).write_bytes(data)
        notice_hashes[name] = expected
    command("codesign", "--force", "--sign", "-", "--timestamp=none", framework)
    command("codesign", "--verify", "--strict", "--verbose=2", framework)
    entitlements = command("codesign", "--display", "--entitlements", ":-", framework, capture=True)
    if entitlements and plistlib.loads(entitlements) != {}:
        raise RuntimeError("Signed dynamic framework contains unexpected entitlements")
    after = verify_macho(binary.read_bytes(), layout)
    if any(before[key] != after[key] for key in ("load_bias", "rx_address", "rw_address")):
        raise RuntimeError("Signing changed the verified guest address layout")
    (directory / "load-commands.txt").write_bytes(command("xcrun", "otool", "-l", binary, capture=True))
    return binary, {"platform": platform, "target": target, "layout": after, "notice_sha256": notice_sha256,
                    "notices": notice_hashes, "entitlements": {}, "signature_verified": True,
                    "unsigned_bytes": unsigned_size,
                    "signed_bytes": binary.stat().st_size, "link_verify_sign_ns": time.perf_counter_ns() - started}
