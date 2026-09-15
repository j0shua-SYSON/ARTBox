"""Obtain the pinned source selection for ART interpreter bring-up.

This selects sources and records their notices; it does not compile or start ART.
The existing source downloader verifies archive and individual file hashes.
"""
import sys
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path

from environment import ROOT, environment
from sources import obtain_files


CATALOG = ROOT / "third_party/art/runtime-sources.json"


def obtain_runtime_sources():
    """Return the verified component directories in the configured source cache."""
    os.environ.update(environment())
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    cache = Path(os.environ["ARTBOX_CACHE_DIR"])
    return {name: obtain_files(name, spec, cache) for name, spec in catalog.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Source inventory JSON output")
    args = parser.parse_args()
    sources = obtain_runtime_sources()
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    output = args.output or Path(os.environ["ARTBOX_BUILD_DIR"]) / "m3/runtime/source-inventory.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    record = {
        "scope": "Verified original source selection; no runtime compilation or execution",
        "catalog_sha256": hashlib.sha256(CATALOG.read_bytes()).hexdigest(),
        "components": {
            name: {"directory": str(path), "commit": catalog[name]["commit"],
                   "files": len(catalog[name]["files"]),
                   "notice": catalog[name]["notice"],
                   "notice_sha256": catalog[name]["notice_sha256"]}
            for name, path in sources.items()
        },
        "runtime_executed": False,
    }
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(sources)} ART source components; inventory: {output}")


if __name__ == "__main__":
    main()
