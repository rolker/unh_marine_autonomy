# Copyright 2026 University of New Hampshire
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the University of New Hampshire nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""
Fingerprints: the hash of a product's inputs (design draft section 9).

"Content, never mtime." A fingerprint answers one question -- *would rebuilding
this product from today's inputs produce the same thing?* -- so it is taken
over the inputs, not over the output, and a changed process is a new
fingerprint rather than a migration.

The schema is **additive**: an input this stage does not have (no trajectory in
a native-tile adapter, no consumer ordering in a producer) is omitted, not
written as null. Omitting rather than nulling matters because it keeps the
fingerprint of a product stable when a later stage learns to record an input
the product never had.
"""

from __future__ import annotations

import hashlib
import json
from typing import Any, Dict, Mapping, Sequence

#: Every input a fingerprint may be taken over (design section 9). A key
#: outside this set is a typo, and a typo'd input would silently produce a
#: fingerprint that no other tool reproduces -- so it is an error.
FINGERPRINT_KEYS = (
    'source_ids',
    'revision_ids',
    'trajectory_id',
    'geometry_revision_id',
    'consumer_ordering',
    'decoder_version',
    'builder_version',
    'cache_method',
)

#: Sets of ids whose order is not an input: two builds that list the same
#: sources in a different order are the same build, so these are sorted. They
#: are SETS, so a repeated id is also de-duplicated: a source named twice is
#: still one input, and counting it twice would give the same build two
#: fingerprints depending on how its caller spelled the list.
_UNORDERED_KEYS = frozenset({'source_ids', 'revision_ids'})

#: Ordering IS the input here (design section 5: "the order it used is an input
#: to the fingerprint of whatever it builds"), so it is never sorted.
_ORDERED_KEYS = frozenset({'consumer_ordering'})


class FingerprintError(ValueError):
    """An input set that cannot be fingerprinted as specified."""


def fingerprint_document(**inputs: Any) -> Dict[str, Any]:
    """
    Normalize ``inputs`` into the document a fingerprint is taken over.

    Returned as well as hashed because the Item records it: a consumer that
    disagrees with a fingerprint can see which input it disagrees about,
    instead of seeing two different hex strings.
    """
    unknown = sorted(set(inputs) - set(FINGERPRINT_KEYS))
    if unknown:
        raise FingerprintError(
            f'unknown fingerprint input(s): {unknown}. Known inputs are '
            f'{list(FINGERPRINT_KEYS)}; extending the schema is a design '
            'change (section 9), not a keyword argument.')
    document: Dict[str, Any] = {}
    for key in FINGERPRINT_KEYS:
        if key not in inputs:
            continue
        value = inputs[key]
        if value is None:
            # An input that is absent is omitted; an input that is present but
            # null would be a third state nobody has defined.
            raise FingerprintError(
                f'{key} is None; omit the input rather than passing null')
        if key in _UNORDERED_KEYS:
            document[key] = sorted(set(_as_str_sequence(key, value)))
        elif key in _ORDERED_KEYS:
            document[key] = list(_as_str_sequence(key, value))
        else:
            if not isinstance(value, str) or not value.strip():
                raise FingerprintError(
                    f'{key} must be a non-empty string, got {value!r}')
            document[key] = value
    if not document:
        raise FingerprintError(
            'a fingerprint over no inputs identifies nothing; name at least '
            'one input')
    return document


def fingerprint(**inputs: Any) -> str:
    """sha256 over the canonical JSON of :func:`fingerprint_document`."""
    return fingerprint_of(fingerprint_document(**inputs))


def fingerprint_of(document: Mapping[str, Any]) -> str:
    """
    Hash an already-normalized fingerprint document.

    Canonical JSON: sorted keys, no insignificant whitespace, UTF-8. Two tools
    that agree on the inputs agree on the hash without agreeing on a library.
    """
    canonical = json.dumps(
        document, sort_keys=True, separators=(',', ':'), ensure_ascii=False)
    return hashlib.sha256(canonical.encode('utf-8')).hexdigest()


def _as_str_sequence(key: str, value: Any) -> Sequence[str]:
    if isinstance(value, str):
        raise FingerprintError(
            f'{key} must be a sequence of ids, not a single string '
            f'({value!r}); a bare string would hash per character')
    try:
        items = list(value)
    except TypeError as exc:
        raise FingerprintError(
            f'{key} must be a sequence of ids, got {type(value).__name__}'
        ) from exc
    if not items:
        raise FingerprintError(f'{key} is empty; omit it instead')
    for item in items:
        if not isinstance(item, str) or not item.strip():
            raise FingerprintError(
                f'{key} contains a non-string or empty id: {item!r}')
    return items


def canonical_json(document: Mapping[str, Any]) -> str:
    """Spell ``document`` the one canonical way every hash here uses."""
    return json.dumps(
        document, sort_keys=True, separators=(',', ':'), ensure_ascii=False)


def content_hash(document: Mapping[str, Any]) -> str:
    """sha256 of a document's canonical JSON -- the changed-Item gate."""
    return hashlib.sha256(canonical_json(document).encode('utf-8')).hexdigest()
