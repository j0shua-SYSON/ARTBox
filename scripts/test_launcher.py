"""Build ARTBox's own launcher and exercise it in disposable iOS simulators.

Uses public Xcode tools on macOS. All generated app, test, simulator and evidence
paths are under the configured project build/artifact locations. The shipped
device app is built separately and never includes the UIKit test hooks.
"""
import sys
sys.dont_write_bytecode = True

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from environment import ROOT, environment


def run(*args, capture=False, timeout=180):
    print("+ " + subprocess.list2cmdline(list(map(str, args))), flush=True)
    result = subprocess.run(list(map(str, args)), capture_output=capture, timeout=timeout, check=True)
    return result.stdout if capture else b""


def main():
    os.environ.update(environment())
    if sys.platform != "darwin":
        raise RuntimeError("Launcher UI checks require Xcode on macOS")
    build = Path(os.environ["ARTBOX_BUILD_DIR"]) / "launcher"
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"]) / "launcher"
    build.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    # Validate real Foundation file operations independently of UIKit.
    library_test = build / "library-test"
    run("xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror", "-I", ROOT / "app",
        "-framework", "Foundation", ROOT / "app/LibraryStore.m", ROOT / "app/tests/library_store.m", "-o", library_test)
    with tempfile.TemporaryDirectory(prefix="library-", dir=build) as temporary:
        run(library_test, temporary)
    run("cmake", "-S", ROOT, "-B", build, "-G", "Xcode", "-DCMAKE_SYSTEM_NAME=iOS",
        "-DCMAKE_OSX_SYSROOT=iphonesimulator", "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
        "-DARTBOX_UI_TESTING=ON", "-DARTBOX_GUEST_BUNDLES=", "-DARTBOX_BUNDLE_ID=org.artbox.launcher.tests",
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY", "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO")
    run("xcodebuild", "-project", build / "ARTBox.xcodeproj", "-scheme", "ARTBox", "-configuration", "Release",
        "-sdk", "iphonesimulator", "-destination", "generic/platform=iOS Simulator",
        "-derivedDataPath", build / "derived-data", f"OBJROOT={build / 'intermediates'}", f"SYMROOT={build / 'products'}",
        f"CLANG_MODULE_CACHE_PATH={os.environ['CLANG_MODULE_CACHE_PATH']}", "CODE_SIGNING_ALLOWED=NO", "build", timeout=300)
    app = build / "Release-iphonesimulator/ARTBox.app"
    run("codesign", "--force", "--sign", "-", "--timestamp=none", app)
    devices = build / "devices"
    devices.mkdir(exist_ok=True)
    sim = ("xcrun", "simctl", "--set", devices)
    available = json.loads(run(*sim, "list", "--json", capture=True))
    runtimes = [runtime for runtime in available["runtimes"] if runtime.get("isAvailable") and
                runtime["identifier"].startswith("com.apple.CoreSimulator.SimRuntime.iOS-")]
    if not runtimes:
        raise RuntimeError("No installed iOS simulator runtime; install one with Xcode before running these checks")
    runtime = max(runtimes, key=lambda r: tuple(int(part) for part in r["version"].split(".")))
    identifiers = {item["identifier"] for item in available["devicetypes"]}
    models = ["iPhone-SE-3rd-generation", "iPhone-16"]
    records = []
    for model in models:
        kind = "com.apple.CoreSimulator.SimDeviceType." + model
        if kind not in identifiers:
            raise RuntimeError(f"Required simulator type is unavailable: {model}")
        device = run(*sim, "create", "ARTBox launcher " + model, kind, runtime["identifier"], capture=True).decode().strip()
        target = artifacts / model
        target.mkdir(exist_ok=True)
        try:
            run(*sim, "boot", device)
            run(*sim, "bootstatus", device, "-b", timeout=180)
            run(*sim, "install", device, app)
            container = Path(run(*sim, "get_app_container", device, "org.artbox.launcher.tests", "data", capture=True).decode().strip())
            report = container / "Documents/LauncherChecks/result.json"
            run(*sim, "launch", device, "org.artbox.launcher.tests")
            deadline = time.monotonic() + 90
            while not report.is_file() and time.monotonic() < deadline:
                time.sleep(0.25)
            if not report.is_file():
                raise RuntimeError(f"Launcher checks did not write their report on {model}")
            for path in report.parent.iterdir():
                if path.suffix in (".json", ".png"):
                    shutil.copyfile(path, target / path.name)
            record = json.loads(report.read_text())
            if not record.get("passed") or record.get("checks", 0) < 20:
                raise RuntimeError(f"Launcher checks failed on {model}: {record}")
            run(*sim, "io", device, "screenshot", target / "simulator.png")
            records.append({"model": model, "runtime": runtime["version"], **record})
        finally:
            # These are only the disposable devices created by this invocation.
            subprocess.run(list(map(str, (*sim, "shutdown", device))), capture_output=True, timeout=30)
            run(*sim, "delete", device, timeout=30)
    (artifacts / "checks.json").write_text(json.dumps({"foundation_store": "passed", "simulators": records}, indent=2) + "\n")
    print("ARTBox launcher, library storage and UIKit checks passed on both simulator sizes")


if __name__ == "__main__":
    main()
