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
``revisions/`` -- reviewed records that change how a source is read.

Design draft section 2 (the category) and section 6 (what they are applied to,
and when: at **link**, never baked into an observation). Two kinds are
exercised by this subset and therefore written here:

* a **geometry revision** -- a dated platform-geometry record (lever arms,
  mounting angles) with a validity interval;
* a **datum record** -- a per-bag or per-interval correction, such as the
  season's NAD83(2011)-labelled-WGS84 positions or the EGM96 round-trip bias.

Cleaning marks, decode revisions and the casters table are the same shape and
the same writer; they are simply not produced by this subset, so no CLI claims
to write one.

**Append-only.** A revision is a record of what a reviewer decided, so it is
never edited in place: writing a different body under an existing id is an
error, and a corrected decision is a new record that supersedes the old one by
id. The id is the content hash, so "a different body under the same id" can
only happen by tampering or by a bug.
"""

from __future__ import annotations

from datetime import date, datetime
from enum import Enum
import json
import math
from pathlib import Path
from typing import Any, Dict, Mapping, Optional, Union

from marine_world_store import atomic_io, item_schema, layout, source_time
from marine_world_store.fingerprint import canonical_json, content_hash

PathLike = Union[str, Path]


class RevisionKind(str, Enum):
    """The kinds of revision record this package writes."""

    GEOMETRY = 'geometry'
    DATUM = 'datum'


#: Fields every revision record carries.
REQUIRED_FIELDS = ('kind', 'applies_to', 'parameters', 'evidence', 'reviewer')


class RevisionError(ValueError):
    """A revision record that is not reviewable as written."""


def build_revision(
    *,
    kind: Union[RevisionKind, str],
    applies_to: Mapping[str, Any],
    parameters: Mapping[str, Any],
    evidence: str,
    reviewer: str,
    valid_from: Optional[str] = None,
    valid_to: Optional[str] = None,
    notes: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Build one append-only revision Item.

    :param applies_to: what the record governs -- ``{"source_id": ...}`` for a
        per-bag correction, or ``{"platform": ..., "from": ..., "to": ...}``
        for a dated platform record. Free-form by kind, because a datum record
        and a geometry revision do not apply to the same sort of thing; it is
        required, so nothing is ever written that applies to nothing.
    :param evidence: what the reviewer looked at. Required: a record with no
        evidence cannot be re-examined, and section 2 calls these *reviewed*
        records.
    :param valid_from: when the record starts applying. **Required**: a
        record is an Item, every Item is dated, and a correction with no
        stated validity start cannot be applied to a given day's data. With
        ``valid_to`` it is written as a range; alone it is the single
        ``datetime`` STAC's other legal shape allows -- a record that still
        applies has no end, which is not the same as an unknown one.
    :raises RevisionError: on a missing or empty required field.
    """
    try:
        kind = RevisionKind(kind)
    except ValueError as exc:
        allowed = ', '.join(sorted(k.value for k in RevisionKind))
        raise RevisionError(
            f'{kind!r} is not a revision kind; expected one of {allowed}'
        ) from exc
    if not isinstance(applies_to, Mapping) or not applies_to:
        raise RevisionError('applies_to must be a non-empty mapping')
    if not isinstance(parameters, Mapping) or not parameters:
        raise RevisionError('parameters must be a non-empty mapping')
    applies_to = _json_safe(applies_to, 'applies_to')
    parameters = _json_safe(parameters, 'parameters')
    for name, value in (('evidence', evidence), ('reviewer', reviewer)):
        if not isinstance(value, str) or not value.strip():
            raise RevisionError(f'{name} must be a non-empty string')
    if not valid_from:
        raise RevisionError(
            'valid_from is required: a record with no stated validity start '
            'cannot be applied to a given day of data, and an Item with no '
            'time is not a STAC Item')
    try:
        if valid_to:
            valid_from, valid_to = source_time.check_interval(
                valid_from, valid_to, what='this revision')
        else:
            valid_from = source_time.as_rfc3339(valid_from, what='valid_from')
    except source_time.TimeIntervalError as exc:
        raise RevisionError(str(exc)) from exc

    properties: Dict[str, Any] = {
        'datetime': None if valid_to else valid_from,
        item_schema.CONTRACT_FIELDS['revision']: {
            'kind': kind.value,
            'applies_to': dict(applies_to),
            'parameters': dict(parameters),
            'evidence': evidence,
            'reviewer': reviewer,
        },
    }
    if valid_to:
        properties['start_datetime'] = valid_from
        properties['end_datetime'] = valid_to
    if notes:
        properties[f'{item_schema.PREFIX}:notes'] = notes

    # No ``collection`` field: STAC 1.0 requires a ``rel: collection`` link
    # whenever it is set, and ``revisions/`` has no Collection document to
    # link to (as for source Items).
    body = {
        'type': 'Feature',
        'stac_version': item_schema.STAC_VERSION,
        'geometry': None,
        'properties': properties,
        'links': [],
        'assets': {},
    }
    body['id'] = revision_id(body)
    return body


def _json_safe(value: Any, where: str) -> Any:
    """
    Return ``value`` as plain JSON, or refuse it naming where it sits.

    YAML is why this exists: an unquoted ``from: 2026-06-01T00:00:00Z`` nested
    in ``applies_to`` arrives as a :class:`~datetime.datetime`, which the
    content hash (canonical JSON) cannot serialise -- the writer died with a
    raw ``TypeError`` traceback. A zoned datetime is spelled canonically, the
    same way every Item time is; a date or a zone-less time is refused with
    the remedy, because it names no instant (as for Item intervals).

    :raises RevisionError: on anything JSON has no spelling for.
    """
    if isinstance(value, Mapping):
        out = {}
        for key, item in value.items():
            if not isinstance(key, str):
                raise RevisionError(
                    f'{where}: key {key!r} is not a string; quote it')
            out[key] = _json_safe(item, f'{where}.{key}')
        return out
    if isinstance(value, (list, tuple)):
        return [_json_safe(item, f'{where}[{index}]')
                for index, item in enumerate(value)]
    if isinstance(value, (datetime, date)):
        try:
            return source_time.as_rfc3339(value, what=where)
        except source_time.TimeIntervalError as exc:
            raise RevisionError(str(exc)) from exc
    if value is None or isinstance(value, (str, bool, int, float)):
        if isinstance(value, float) and not math.isfinite(value):
            raise RevisionError(
                f'{where}: {value!r} is not a JSON number')
        return value
    raise RevisionError(
        f'{where}: a {type(value).__name__} has no JSON spelling; write it as '
        'a string or a number')


def revision_id(body: Mapping[str, Any]) -> str:
    """Compute the record's id: the hash of its content, minus the id."""
    without_id = {k: v for k, v in body.items() if k != 'id'}
    return content_hash(without_id)


def write_revision(root: PathLike, revision: Mapping[str, Any]) -> Path:
    """
    Write ``revision`` under ``<root>/revisions/``, append-only.

    Re-writing an identical record is a no-op (the id is the content hash, so
    "identical" is exact). A different body under the same id raises rather
    than overwriting: that is either tampering or a bug in the id, and both
    are worth stopping for.

    :returns: the path written (or the path that already held the record).
    """
    directory = layout.revisions_dir(root)
    directory.mkdir(parents=True, exist_ok=True)
    identifier = revision.get('id')
    if not identifier:
        raise RevisionError('revision has no id; build it with build_revision')
    if identifier != revision_id(revision):
        raise RevisionError(
            f'revision id {identifier!r} does not match its content; the id is '
            'the content hash and is never assigned by hand')
    path = directory / f'{identifier}.json'
    text = json.dumps(revision, indent=2, sort_keys=True) + '\n'
    if path.exists():
        existing = json.loads(path.read_text())
        if canonical_json(existing) != canonical_json(revision):
            raise RevisionError(
                f'{path} already holds a different record under the same id; '
                'revisions are append-only and are never edited in place')
        return path
    _atomic_write(path, text)
    return path


def read_revision(path: PathLike) -> Dict[str, Any]:
    """Read one revision record and verify its id against its content."""
    path = Path(path)
    body = json.loads(path.read_text())
    if body.get('id') != revision_id(body):
        raise RevisionError(
            f'{path}: content does not hash to its recorded id; the record has '
            'been edited in place')
    return body


def _atomic_write(path: Path, text: str) -> None:
    """Publish whole (:mod:`marine_world_store.atomic_io`)."""
    atomic_io.write_text(path, text)
