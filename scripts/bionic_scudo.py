"""Retain Android's Scudo configuration with a smaller signed-runtime arena."""
# SPDX-License-Identifier: MIT
import hashlib
from pathlib import Path
import re


def check_crc_code(source, assembly):
    """Optional ARM64 CRC instructions may occur only in the gated helper unit."""
    count = len(re.findall(r'\bcrc32c?[bhwx]\b', assembly))
    if source != 'scudo/standalone/crc32_hw.cpp' and count:
        raise RuntimeError('Unconditional optional CRC instructions in ' + source)
    if source == 'scudo/standalone/crc32_hw.cpp' and count != 1:
        raise RuntimeError('The dedicated Scudo hardware CRC helper changed')
    return count


def prepare(source, output, expected, native):
    original = Path(source) / 'config/custom_scudo_config.h'
    data = original.read_bytes()
    digest = lambda value: hashlib.sha256(value).hexdigest()
    if digest(data) != expected:
        raise RuntimeError('Scudo Android configuration differs from the reviewed source')
    adapted = data
    if native:
        # Select only AndroidNormalConfig's ARM64 region size. Keep the original
        # class map, randomization, TSDs, release policy and secondary allocator.
        start = data.index(b'struct AndroidNormalConfig {')
        end = data.index(b'struct AndroidLowMemoryConfig {', start)
        before = data[start:end]
        old = b'static const uptr RegionSizeLog = 28U;'
        if before.count(old) != 1:
            raise RuntimeError('Scudo Android normal primary configuration changed')
        after = before.replace(old, b'static const uptr RegionSizeLog = 24U;')
        adapted = data[:start] + after + data[end:]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'custom_scudo_config.h').write_bytes(adapted)
    (output / 'original_custom_scudo_config.h').write_bytes(data)
    return {'source_sha256': expected, 'configured_sha256': digest(adapted),
            'normal_primary_region_size_log': 24 if native else 28,
            'normal_primary_classes': 33, 'contiguous_reservation_bytes': 33 << (24 if native else 28),
            'change': 'AndroidNormalConfig primary RegionSizeLog only' if native else 'none'}
