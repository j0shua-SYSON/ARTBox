"""Build both Bionic profiles and compare the adaptation with the upstream control."""
import sys

sys.dont_write_bytecode = True

import argparse
import json
import os
from pathlib import Path
import subprocess

from environment import ROOT, environment
from bionic_adapt import check_native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int)
    args = parser.parse_args()
    os.environ.update(environment())
    artifacts = Path(os.environ["ARTBOX_ARTIFACTS_DIR"])
    reports = {}
    for profile in ("upstream", "native"):
        command = [sys.executable, "-B", str(ROOT / "scripts/build_bionic.py"), "--profile", profile]
        if args.ndk_root:
            command += ["--ndk-root", str(args.ndk_root)]
        if args.build_dir:
            command += ["--build-dir", str(args.build_dir / profile)]
        if args.jobs is not None:
            command += ["--jobs", str(args.jobs)]
        subprocess.run(command, check=True)
        reports[profile] = json.loads((artifacts / f"m2-bionic-{profile}.json").read_text(encoding="utf-8"))
    upstream, native = reports["upstream"], reports["native"]
    if any(upstream[key] != native[key] for key in ("source_commit", "selection_sha256", "compiler")):
        raise RuntimeError("The Bionic profiles do not share a source selection and toolchain")
    control = upstream["native_boundary_inventory"]
    if not all(control[name] > 0 for name in ("tpidr_el0_read", "tpidr_el0_write", "x18_mentions")):
        raise RuntimeError("The upstream Bionic control no longer exercises the adapted instruction classes")
    check_native(native["native_boundary_inventory"], native["undefined_symbols"])
    if native["defined_symbols"] != upstream["defined_symbols"] or "__set_tls" not in native["defined_symbols"]:
        raise RuntimeError("Bionic adaptation changed the set of global definitions")
    if [entry["source"] for entry in native["compiled"]] != [entry["source"] for entry in upstream["compiled"]]:
        raise RuntimeError("The Bionic profiles do not compile the same selected sources")
    result = {"scope": "Bionic source-profile comparison; no guest execution or M2 acceptance",
              "source_commit": native["source_commit"], "compiled_units": len(native["compiled"]),
              "global_definitions": len(native["defined_symbols"]), "same_global_definitions": True,
              "upstream_object_sha256": upstream["partial_object_sha256"],
              "native_object_sha256": native["partial_object_sha256"],
              "upstream_inventory": control, "native_inventory": native["native_boundary_inventory"],
              "unresolved_dependencies": len(native["undefined_symbols"])}
    (artifacts / "m2-bionic-profile-check.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Bionic profiles preserve {result['global_definitions']} global definitions across {result['compiled_units']} units")
    print("Native profile has no checked thread-register/syscall instructions; remaining imports stay unresolved")


if __name__ == "__main__":
    main()
