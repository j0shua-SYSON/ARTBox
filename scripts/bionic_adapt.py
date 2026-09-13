"""Source adaptation and object-code checks for the pinned Bionic build."""
import hashlib
from pathlib import PurePosixPath
import re
import shutil


def adapt_sources(source, overlay, patch):
    source, overlay = source.resolve(), overlay.resolve()
    if source.is_relative_to(overlay) or overlay.is_relative_to(source):
        raise RuntimeError("The Bionic overlay must be separate from its original source")
    prepared, records, seen = [], [], set()
    for edit in patch["files"]:
        name = PurePosixPath(edit["path"])
        if name.is_absolute() or not name.parts or ".." in name.parts or "\\" in str(name) or ":" in str(name):
            raise RuntimeError("Unsafe Bionic patch path")
        if str(name).casefold() in seen:
            raise RuntimeError("Duplicate Bionic patch path")
        seen.add(str(name).casefold())
        path, output = source / str(name), overlay / str(name)
        if not path.resolve().is_relative_to(source) or not path.is_file() or not output.resolve().is_relative_to(overlay):
            raise RuntimeError(f"Invalid Bionic patch input: {name}")
        original = path.read_bytes()
        upstream_hash = hashlib.sha256(original).hexdigest()
        if upstream_hash != edit["sha256"]:
            raise RuntimeError(f"Bionic patch source hash changed: {name}")
        contents = original.decode("utf-8")
        for replacement in edit["replacements"]:
            before = replacement["before"]
            if not before or contents.count(before) != 1:
                raise RuntimeError(f"Bionic patch context is missing or ambiguous: {name}")
            contents = contents.replace(before, replacement["after"], 1)
        data = contents.encode("utf-8")
        prepared.append((output, data))
        records.append({"path": str(name), "upstream_sha256": upstream_hash,
                        "adapted_sha256": hashlib.sha256(data).hexdigest()})
    # Validate all edits before writing; preserve adjacent quoted header includes.
    shutil.copytree(source / "libc/platform", overlay / "libc/platform", dirs_exist_ok=True)
    for output, data in prepared:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
    return records


def inventory(disassembly):
    # Ignore symbol labels and paths; a checkout directory named x18 is not an
    # instruction using x18. LLVM is invoked with --no-show-raw-insn.
    instructions = "\n".join(re.findall(r"^\s*[0-9a-f]+:\s+(.+)$", disassembly, re.MULTILINE | re.IGNORECASE))
    patterns = {"svc": r"\bsvc\s+", "tpidr_el0_read": r"\bmrs\s+[^,\n]+,\s*tpidr_el0\b",
                "tpidr_el0_write": r"\bmsr\s+tpidr_el0\b", "tpidr_mentions": r"\btpidr\w*\b",
                "x18_mentions": r"\b[wx]18\b", "x27_mentions": r"\b[wx]27\b",
                "x28_mentions": r"\b[wx]28\b", "unknown_instructions": r"<unknown>"}
    return {"instruction_count": len(instructions.splitlines()),
            **{name: len(re.findall(pattern, instructions, re.IGNORECASE)) for name, pattern in patterns.items()}}


def check_native(counts, undefined):
    unsafe = {name: count for name, count in counts.items() if name != "instruction_count" and count}
    symbols = {entry["symbol"] for entry in undefined}
    if unsafe or not counts["instruction_count"]:
        raise RuntimeError(f"Bionic native profile retains unsupported instruction accesses: {unsafe}")
    if any(symbol.startswith("__aarch64_") for symbol in symbols):
        raise RuntimeError("Bionic native profile still imports outlined atomic/runtime helpers")
    if not {"artbox_bionic_get_tls", "artbox_bionic_set_tls", "__stack_chk_fail"} <= symbols:
        raise RuntimeError("Bionic native profile lost its guest TLS bridge or stack protection")
