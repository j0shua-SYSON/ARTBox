"""Adapt only the pinned ART raw-pointer reference codec; preserve upstream text."""
import hashlib

SOURCE = 'runtime/mirror/object_reference.h'
SHA256 = 'cb97ef1a4c0ffab7ab4cf6ba1185e26bb757e06abdcf7998baf291e7f34e5bae'
REPLACEMENTS = (
    ('#include "base/atomic.h"',
     '// ARTBox: heap-base-relative references for the signed native contract.\n'
     '#include "artbox_art_reference_bridge.h"\n\n#include "base/atomic.h"'),
    ('uint32_t as_bits = reinterpret_cast32<uint32_t>(mirror_ptr);',
     'uint32_t as_bits = artbox_art_reference_compress(mirror_ptr);'),
    ('return reinterpret_cast32<MirrorType*>(as_bits);',
     'return static_cast<MirrorType*>(artbox_art_reference_decompress(as_bits));'),
)


def adapt(original):
    if hashlib.sha256(original).hexdigest() != SHA256:
        raise RuntimeError('ART reference input differs from the reviewed upstream header')
    text = original.decode('utf-8')
    for before, after in REPLACEMENTS:
        if text.count(before) != 1:
            raise RuntimeError('ART reference adaptation context is missing or ambiguous')
        text = text.replace(before, after, 1)
    return text.encode('utf-8')
