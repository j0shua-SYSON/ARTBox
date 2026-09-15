"""Apply reviewed host-build edits to a verified copy of the ART selection."""
import hashlib
from pathlib import PurePosixPath


def adapt_sources(source, output, selection, patch):
    source, output = source.resolve(), output.resolve()
    if source.is_relative_to(output) or output.is_relative_to(source):
        raise RuntimeError('ART host sources must be separate from the original cache')
    prepared, seen = {}, set()
    for item in selection:
        name = item['path']
        path = PurePosixPath(name)
        if (path.is_absolute() or not path.parts or '..' in path.parts or
                '\\' in name or ':' in name or name.casefold() in seen):
            raise RuntimeError('Unsafe or duplicate ART selection path: ' + name)
        seen.add(name.casefold())
        original, target = source / name, output / name
        if not original.resolve().is_relative_to(source) or not target.resolve().is_relative_to(output):
            raise RuntimeError('ART source path escapes its tree: ' + name)
        data = original.read_bytes()
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise RuntimeError('ART source changed: ' + name)
        prepared[name] = data
    records, edited = [], set()
    for item in patch['files']:
        name = item['path']
        if name not in prepared or name in edited:
            raise RuntimeError('ART adaptation is duplicate or outside the selection: ' + name)
        edited.add(name)
        original = prepared[name]
        if hashlib.sha256(original).hexdigest() != item['sha256']:
            raise RuntimeError('ART adaptation source hash changed: ' + name)
        text = original.decode('utf-8')
        for replacement in item['replacements']:
            before = replacement['before']
            if not before or text.count(before) != 1:
                raise RuntimeError('ART adaptation context is missing or ambiguous: ' + name)
            text = text.replace(before, replacement['after'], 1)
        prepared[name] = text.encode('utf-8')
        records.append({'path': name, 'upstream_sha256': item['sha256'],
                        'adapted_sha256': hashlib.sha256(prepared[name]).hexdigest()})
    # Validate everything first. Copy adjacent headers too: quoted includes search
    # the including file's directory before command-line include directories.
    if output.exists() and any(p.relative_to(output).as_posix() not in prepared
                               for p in output.rglob('*') if p.is_file()):
        raise RuntimeError('ART host source tree contains files outside this selection')
    for name, data in prepared.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return records
