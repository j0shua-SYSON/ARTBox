"""Instruction and dependency boundaries for the signed Android ICU libraries."""
from bionic_adapt import inventory


def check_code(disassembly):
    counts = inventory(disassembly)
    if not counts['instruction_count'] or any(value for key, value in counts.items() if key != 'instruction_count'):
        raise ValueError('ICU code contains a kernel, thread-pointer, reserved-register or unknown instruction')
    return counts


def check_dependencies(images, checked):
    scopes = {}
    for name in checked:
        pending, scope = [name], []
        while pending:
            current = pending.pop(0)
            if current in scope:
                continue
            if current not in images:
                raise ValueError('Missing DT_NEEDED library: ' + current)
            scope.append(current)
            pending.extend(images[current]['needed'])
        provided = set().union(*(set(images[item]['exports']) for item in scope))
        missing = sorted(symbol for symbol, kind in images[name]['imports'].items()
                         if kind == 'U' and symbol not in provided)
        if missing:
            raise ValueError('Unresolved strong imports in ' + name + ': ' + repr(missing))
        scopes[name] = scope
    return scopes
