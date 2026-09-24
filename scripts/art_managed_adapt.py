"""Compose the reviewed high-heap edits before validating original ART sources."""
import json
from pathlib import Path
from art_reference_adapt import SOURCE, SHA256, REPLACEMENTS

ROOT = Path(__file__).resolve().parents[1]


def managed_boundary():
    files = [{'path': SOURCE, 'sha256': SHA256,
              'replacements': [{'before': a, 'after': b} for a, b in REPLACEMENTS]}]
    for name in ('managed-storage-boundary.json', 'interpreter-arguments-boundary.json',
                 'managed-window-boundary.json', 'class-table-boundary.json'):
        files.extend(json.loads((ROOT / 'third_party/art' / name).read_text(encoding='utf-8'))['files'])
    return files
