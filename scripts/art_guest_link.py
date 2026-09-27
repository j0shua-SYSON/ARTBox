"""Instruction and import boundaries for the complete precompiled ART guest."""
import re
from bionic_adapt import inventory

HOST_IMPORTS = frozenset('artbox_bionic_get_tls artbox_vm_access artbox_vm_mmap_window '
                        'artbox_vm_mprotect artbox_vm_munmap artbox_vm_page_size '
                        'artbox_vm_reserve_window artbox_vm_reserved_bytes'.split())
SNAPSHOTS = frozenset([
    '_ZN3art18BacktraceCollector11CollectImplEPN11unwindstack8UnwinderE',
    '_ZN11unwindstack20AndroidLocalUnwinder14InternalUnwindENSt6__ndk18optionalIiEERNS_19AndroidUnwinderDataE',
    'unw_getcontext'])


def check_imports(imports, dependency_exports):
    missing = sorted(name for name, kind in imports.items() if kind == 'U' and name not in dependency_exports)
    if missing != sorted(HOST_IMPORTS):
        raise ValueError('Full ART imports differ from the eight explicit VM/TLS bindings: ' + repr(missing))
    return missing


def check_code(disassembly):
    counts = inventory(disassembly)
    if not counts['instruction_count'] or any(counts[k] for k in ('svc', 'tpidr_mentions', 'unknown_instructions')):
        raise ValueError('Full ART code retains a kernel, thread-pointer or unknown instruction')
    current, reads = None, []
    for line in disassembly.splitlines():
        label = re.match(r'^\s*[0-9a-f]+ <(.+)>:$', line)
        if label:
            current = label.group(1)
            continue
        instruction = re.match(r'^\s*[0-9a-f]+:\s+(.+)$', line)
        if not instruction or not re.search(r'\b[wx]18\b', instruction.group(1)):
            continue
        text = instruction.group(1).strip()
        if current not in SNAPSHOTS or not re.fullmatch(r'stp\s+x18,\s*x19,\s*\[x(?:0|8),\s*#0x90\]', text):
            raise ValueError('Unreviewed ART platform-register instruction: ' + str(current) + ': ' + text)
        reads.append({'symbol': current, 'instruction': text})
    if len(reads) != len(SNAPSHOTS) or {r['symbol'] for r in reads} != SNAPSHOTS:
        raise ValueError('Full ART register snapshot coverage changed')
    return {'inventory': counts, 'platform_register_reads': reads}
