"""Narrow source adaptation for optional Rust stack-trace name formatting."""
import hashlib


def without_rust_demangler(source):
    data = source.read_bytes()
    expected = '48ecc10746919366b620a9bff1c7e6e729e27a952bdf6e5a9ebf11a90f3f1416'
    if hashlib.sha256(data).hexdigest() != expected:
        raise RuntimeError('Unwindstack demangler differs from the reviewed upstream source')
    text = data.decode('utf-8')
    include = '#include <rustc_demangle.h>'
    branch = '''  } else if (name[1] == 'R') {
    // Try to demangle rust name.
    demangled_str = rustc_demangle(name.c_str(), nullptr, nullptr, nullptr);'''
    if text.count(include) != 1 or text.count(branch) != 1:
        raise RuntimeError('Unwindstack demangling context is missing or ambiguous')
    text = text.replace(include,
        '// ARTBox: keep Rust trace labels verbatim when its demangler is omitted.\n'
        '#if !defined(ARTBOX_NO_RUST_DEMANGLE)\n' + include + '\n#endif', 1)
    return text.replace(branch, '#if !defined(ARTBOX_NO_RUST_DEMANGLE)\n' + branch + '\n#endif', 1).encode('utf-8')
