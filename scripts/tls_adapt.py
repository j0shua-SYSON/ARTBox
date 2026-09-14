"""Adapt controlled Clang global-dynamic TLS assembly before signing.

The caller supplies compiler output for TLS-only source. This is not an
arbitrary binary rewriter. Keep original output for native Linux execution.
"""
import re


def adapt(assembly):
    # Initial/local-exec TLS has a different offset contract. Link with --no-relax.
    if re.search(r":(?:gottprel|tprel|dtprel)|\b(?:tpidrro|tpidr2)_el0\b", assembly, re.I):
        raise ValueError("Unsupported TLS access model")
    calls = re.findall(r"^\s*\.tlsdesccall\s+\S+\s*$", assembly, re.M)
    reads = []

    def replace(match):
        register = match.group(1)
        if int(register[1:]) in (18, 27, 28, 29, 30, 31):
            raise ValueError("Reserved register in TLS access")
        reads.append(register)
        return f"\tmov\t{register}, xzr // ARTBox absolute TLSDESC address"

    changed = re.sub(r"^\s*mrs\s+(x\d+),\s*TPIDR_EL0\s*$", replace, assembly, flags=re.M | re.I)
    if not calls or not reads or re.search(r"\b(?:mrs|msr)\b[^\n]*\bTPIDR", changed, re.I):
        raise ValueError("Missing TLSDESC access or unadapted thread-pointer instruction")
    # Clang may hoist one TP read across multiple TLSDESC accesses. Every read
    # in this controlled source has a zero-base contract; no runtime code edits.
    return changed, {"tlsdesc_calls": len(calls), "tp_reads_replaced": len(reads)}
