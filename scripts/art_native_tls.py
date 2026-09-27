"""Apply the reviewed HeapSampler TLS boundary to pinned ART compiler output."""
import hashlib
from pathlib import Path
import re
import subprocess
from bionic_adapt import inventory
from tls_adapt import adapt


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def adapt_assembly(source, assembly, rule):
    if hashlib.sha256(source).hexdigest() != rule['source_sha256']:
        raise ValueError('ART TLS source differs from its reviewed selection')
    symbols = sorted(set(re.findall(r'^\s*\.tlsdesccall\s+(\S+)\s*$', assembly, re.M)))
    if symbols != rule['tls_symbols']:
        raise ValueError('Unexpected ART compiler TLS variable')
    changed, edits = adapt(assembly)
    if any(edits[k] != rule[k] for k in ('tlsdesc_calls', 'tp_reads_replaced')):
        raise ValueError('ART compiler TLS access count changed')
    return changed, edits


def compile_adaptation(command, source, original_object, output, unit, rule):
    output.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(words, label):
        words = list(map(str, words))
        process = subprocess.run(words, capture_output=True)
        (output / (unit + '-' + label + '.log')).write_bytes(process.stdout + process.stderr)
        commands.append(words)
        if process.returncode:
            raise RuntimeError('ART TLS compilation failed: ' + unit + '/' + label)
        return process.stdout.decode('utf-8')

    original = output / (unit + '.original.s')
    assembly_command = list(command)
    assembly_command[assembly_command.index('-c')] = '-S'
    assembly_command[assembly_command.index('-o') + 1] = str(original)
    run(assembly_command, 'assembly')
    changed, edits = adapt_assembly(source.read_bytes(), original.read_text(encoding='utf-8'), rule)
    adapted = output / (unit + '.adapted.s')
    adapted.write_text(changed, encoding='utf-8')
    compiler = Path(command[0])
    objdump = compiler.parent / ('llvm-objdump.exe' if compiler.suffix == '.exe' else 'llvm-objdump')
    record = {'edits': edits, 'source_sha256': digest(source), 'commands': commands,
              'original_assembly_sha256': digest(original), 'adapted_assembly_sha256': digest(adapted)}
    for mode, assembly in [('original', original), ('adapted', adapted)]:
        obj = output / (unit + '.' + mode + '.o')
        run([compiler, '--target=aarch64-linux-android35', '-march=armv8-a', '-c', assembly, '-o', obj], mode + '-assemble')
        counts = inventory(run([objdump, '-d', '--no-show-raw-insn', obj], mode + '-disassembly'))
        record[mode] = {'object_sha256': digest(obj), 'inventory': counts}
        if mode == 'original' and digest(obj) != digest(original_object):
            raise RuntimeError('ART assembly does not reproduce its unmodified object: ' + unit)
        if mode == 'adapted' and (not counts['instruction_count'] or
                                 any(v for k, v in counts.items() if k != 'instruction_count')):
            raise RuntimeError('ART TLS object retains unsafe instructions: ' + unit)
    if record['original']['inventory']['instruction_count'] != record['adapted']['inventory']['instruction_count']:
        raise RuntimeError('ART TLS adaptation changed the instruction count: ' + unit)
    original_object.write_bytes((output / (unit + '.adapted.o')).read_bytes())
    return record
