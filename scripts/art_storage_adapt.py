"""Prepare checked ART storage adaptations without changing the upstream cache."""
from art_host_adapt import adapt_sources
from art_reference_adapt import SOURCE, SHA256, REPLACEMENTS


def adapt_storage(source, output, selection, boundary):
    reference = {'path': SOURCE, 'sha256': SHA256,
                 'replacements': [{'before': before, 'after': after}
                                  for before, after in REPLACEMENTS]}
    return adapt_sources(source, output, selection,
                         {'files': [reference, *boundary['files']]})
