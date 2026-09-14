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
    if any(upstream[key] != native[key] for key in ("source_commit", "selection_sha256", "component_selection_sha256", "dependencies", "compiler")):
        raise RuntimeError("The Bionic profiles do not share a source selection and toolchain")
    control = upstream["native_boundary_inventory"]
    if not all(control[name] > 0 for name in ("tpidr_el0_read", "tpidr_el0_write", "x18_mentions", "svc")):
        raise RuntimeError("The upstream Bionic control no longer exercises the adapted instruction classes")
    check_native(native["native_boundary_inventory"], native["undefined_symbols"], native["stack_protection"])
    if upstream["binary128"] != native["binary128"]:
        raise RuntimeError("Both profiles must use the same reviewed compiler runtime and arithmetic caller")
    policy = json.loads((ROOT / "third_party/bionic/components.json").read_text(encoding="utf-8"))["profile_symbol_differences"]
    upstream_names, native_names = set(upstream["defined_symbols"]), set(native["defined_symbols"])
    if upstream_names - native_names != set(policy["upstream_only"]) or \
            native_names - upstream_names != set(policy["native_only"]) or "__set_tls" not in native_names:
        raise RuntimeError("Bionic adaptation changed definitions outside its explicit allocator ABI policy")
    for profile, report in reports.items():
        if not set(policy[profile + "_only"]).issubset(report["weak_definitions"]):
            raise RuntimeError("An allocator template/TLS difference changed a strong definition")
    gwp_tls = "_ZZN8gwp_asan15getThreadLocalsEvE6Locals"
    if upstream["tls_definitions"] != {gwp_tls: 8} or native["tls_definitions"]:
        raise RuntimeError("Allocator TLS no longer matches the single adapted eight-byte object")
    if [entry["source"] for entry in native["compiled"]] != [entry["source"] for entry in upstream["compiled"]]:
        raise RuntimeError("The Bionic profiles do not compile the same selected sources")
    result = {"scope": "Bionic source-profile comparison; no guest execution or M2 acceptance",
              "source_commit": native["source_commit"], "compiled_units": len(native["compiled"]),
              "global_definitions": len(native["defined_symbols"]), "shared_global_definitions": len(native_names & upstream_names),
              "expected_weak_definition_differences": policy,
              "upstream_object_sha256": upstream["partial_object_sha256"],
              "native_object_sha256": native["partial_object_sha256"],
              "upstream_inventory": control, "native_inventory": native["native_boundary_inventory"],
              "native_stack_protection": native["stack_protection"],
              "unresolved_dependencies": len(native["undefined_symbols"])}
    (artifacts / "m2-bionic-profile-check.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Bionic profiles share {result['shared_global_definitions']} global definitions across {result['compiled_units']} units; allocator weak differences match the explicit policy")
    print("Native profile has no checked thread-register/syscall instructions; remaining imports stay unresolved")


if __name__ == "__main__":
    main()
