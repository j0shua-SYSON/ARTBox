"""Configurable build paths shared by Python commands."""

import argparse
import json
import os
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parent.parent


def environment(overrides=None):
    """Return a child environment; preserve user cache, SDK and credential settings."""
    env = dict(os.environ if overrides is None else overrides)

    def directory(key, default):
        path = Path(env.get(key, default)).expanduser().resolve()
        path.mkdir(parents=True, exist_ok=True)
        env[key] = str(path)
        return path

    env["ARTBOX_ROOT"] = str(ROOT)
    cache = directory("ARTBOX_CACHE_DIR", ROOT / ".env")
    scratch = directory("ARTBOX_TEMP_DIR", ROOT / ".tmp")
    directory("ARTBOX_BUILD_DIR", ROOT / "build")
    directory("ARTBOX_ARTIFACTS_DIR", ROOT / "artifacts")
    for key in ("TEMP", "TMP", "TMPDIR"):
        env[key] = str(scratch)
    for key, subdir in {
        "XDG_CACHE_HOME": "cache", "PIP_CACHE_DIR": "pip",
        "PYTHONPYCACHEPREFIX": "python-pycache", "PYTHONUSERBASE": "python-user",
        "npm_config_cache": "npm-cache", "CARGO_HOME": "cargo", "RUSTUP_HOME": "rustup",
        "GRADLE_USER_HOME": "gradle", "MAVEN_USER_HOME": "maven",
        "ANDROID_USER_HOME": "android-user", "ANDROID_AVD_HOME": "android-avd",
        "CCACHE_DIR": "ccache", "SCCACHE_DIR": "sccache",
        "NUGET_PACKAGES": "nuget", "UV_CACHE_DIR": "uv-cache",
        "UV_PYTHON_INSTALL_DIR": "uv-python", "HOMEBREW_CACHE": "homebrew-cache",
        "HOMEBREW_LOGS": "homebrew-logs", "GOCACHE": "go-cache",
        "GOMODCACHE": "go-mod", "CLANG_MODULE_CACHE_PATH": "clang-modules",
        "SWIFT_MODULECACHE_PATH": "swift-modules",
    }.items():
        env.setdefault(key, str(cache / subdir))
    sdk = env.get("ANDROID_HOME") or env.get("ANDROID_SDK_ROOT") or str(cache / "android-sdk")
    env.setdefault("ANDROID_HOME", sdk)
    env.setdefault("ANDROID_SDK_ROOT", sdk)
    env.setdefault("HOMEBREW_TEMP", str(scratch))
    env.setdefault("LLVM_PROFILE_FILE", str(scratch / "coverage-%p.profraw"))
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["CMAKE_EXPORT_NO_PACKAGE_REGISTRY"] = "ON"
    env["CMAKE_EXPORT_PACKAGE_REGISTRY"] = "OFF"
    # Interactive users keep their home, Git identity, gh login and signing
    # configuration by default. Build agents can opt into a separate home.
    if env.get("ARTBOX_ISOLATE_HOME") == "1":
        home = cache / "home"
        home.mkdir(exist_ok=True)
        for key in ("HOME", "USERPROFILE", "CFFIXED_USER_HOME"):
            env[key] = str(home)
        for key, subdir in {
            "APPDATA": "appdata", "LOCALAPPDATA": "localappdata",
            "XDG_CONFIG_HOME": "config", "XDG_DATA_HOME": "data", "XDG_STATE_HOME": "state",
        }.items():
            env[key] = str(cache / subdir)
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--format", choices=("json", "sh"), default="json")
    args = parser.parse_args()
    configured = environment()
    changed = {key: value for key, value in configured.items() if os.environ.get(key) != value}
    if args.format == "json":
        print(json.dumps(changed, indent=2))
    else:
        print("\n".join(f"export {key}={shlex.quote(value)}" for key, value in changed.items()))


if __name__ == "__main__":
    main()
