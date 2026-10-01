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
Write the store's STAC Items and Collections.

:mod:`marine_world_store.item_schema` builds the documents; this module
validates them against the STAC specification with ``pystac`` and writes them
to disk. The split is deliberate -- the schema is the contract and must be
testable where ``pystac`` is not installed, while validation at write time is
what keeps a hand-built Item from entering the store malformed.

**An undated Item is never written.** A null ``datetime`` with no range
beside it is not a STAC Item, and pystac refuses it -- but the refusal comes
out as a schema error about a document the store should never have built. The
check here names the rule instead: every Item carries its observation
interval, derived from its sources (design draft Part 2 line 2, operator
decision 2026-09-22).

**Only changed Items are rewritten** (design section 9's replica rule: "write
only changed Items"). A rewrite is gated on the canonical-JSON content hash of
the document, not on an mtime, so regenerating an unchanged store touches
nothing and ``rclone bisync`` moves nothing.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import (Any, Dict, Iterable, List, Mapping, Optional, Sequence,
                    Tuple, Union)
import warnings

from marine_world_store import atomic_io, item_schema, layout
from marine_world_store.fingerprint import canonical_json
import pystac

PathLike = Union[str, Path]


class CatalogError(RuntimeError):
    """An Item or Collection that could not be written as specified."""


class ValidatorUnavailable(UserWarning):
    """The STAC validator could not run, so nothing was checked."""


class ValidatorMissing(ValidatorUnavailable):
    """
    The STAC validator cannot run on this host AT ALL -- validation is off.

    Distinct from a schema host that is unreachable for now: this is permanent
    until the host's packages change. pystac 1.9's validator needs
    ``jsonschema >= 4.18`` together with ``referencing``; Ubuntu Noble's apt
    ``python3-jsonschema`` is 4.10 with no ``referencing``, so on an apt-only
    Noble host no Item or Collection is ever schema-checked.
    """


#: What pystac's validator needs, named in the ValidatorMissing message.
VALIDATOR_REQUIREMENT = 'jsonschema >= 4.18 and referencing'


def _validate(document: Mapping[str, Any], stac_type) -> None:
    """
    Validate one document, distinguishing *invalid* from *unchecked*.

    Three outcomes, and they are not the same thing:

    * the document is not a STAC object at all, or fails the schema -- an
      error, because a malformed Item must not enter the store;
    * the validator cannot run on this host at all (pystac cannot load its
      ``jsonschema`` backend) -- :class:`ValidatorMissing`, a warning that says
      validation is OFF and names what it needs, because it is permanent and
      silent otherwise;
    * the validator runs but cannot fetch the remote schema (a boat is offline
      by default) -- :class:`ValidatorUnavailable`, and the write proceeds.

    Neither of the last two refuses the write: a validator that could not run
    reports no verdict, and the alternative is refusing to record data because
    a schema host is down or a host package is old.
    """
    try:
        obj = stac_type.from_dict(dict(document))
    except Exception as exc:  # noqa: BLE001 - pystac raises several types
        raise CatalogError(
            f'{stac_type.__name__} {document.get("id")!r} is malformed: {exc}'
        ) from exc
    invalid = getattr(pystac, 'STACValidationError', None) or getattr(
        getattr(pystac, 'errors', None), 'STACValidationError', None)
    try:
        obj.validate()
    except Exception as exc:  # noqa: BLE001 - narrowed immediately below
        if invalid is not None and isinstance(exc, invalid):
            raise CatalogError(
                f'invalid {stac_type.__name__} {document.get("id")!r}: {exc}'
            ) from exc
        if isinstance(exc, ImportError):
            # pystac raises a bare ImportError when its jsonschema validator
            # cannot be instantiated -- the host's packages, not the network.
            warnings.warn(
                f'STAC VALIDATION IS OFF on this host: pystac cannot run its '
                f'validator ({exc}); it needs {VALIDATOR_REQUIREMENT} (apt '
                f'Noble ships jsonschema 4.10 without referencing). '
                f'{document.get("id")!r} and every other Item this process '
                f'writes are NOT schema-checked.',
                ValidatorMissing, stacklevel=3)
            return
        warnings.warn(
            f'STAC validation skipped for {document.get("id")!r}: {exc}',
            ValidatorUnavailable, stacklevel=3)


def validate_item(item: Mapping[str, Any]) -> None:
    """Validate ``item`` against the STAC spec (see :func:`_validate`)."""
    _validate(item, pystac.Item)


def validate_collection(collection: Mapping[str, Any]) -> None:
    """Validate ``collection`` against the STAC spec."""
    _validate(collection, pystac.Collection)


def check_dated(item: Mapping[str, Any]) -> None:
    """
    Refuse an Item with no time a consumer could search it by.

    STAC allows two shapes: a ``datetime``, or a null one with **both**
    ``start_datetime`` and ``end_datetime``. Anything else is invalid, and an
    Item that reached this point undated means a producer had no interval and
    wrote one anyway.

    :raises CatalogError: naming the Item and the rule.
    """
    properties = item.get('properties') or {}
    if properties.get('datetime') is not None:
        return
    if properties.get('start_datetime') and properties.get('end_datetime'):
        return
    raise CatalogError(
        f'Item {item.get("id")!r} has no observation interval: its datetime '
        'is null and it carries no start_datetime/end_datetime pair. Every '
        'Item is dated from its sources; a product that cannot be dated is '
        'not written (design draft Part 2 line 2).')


def write_item(
    directory: PathLike,
    item: Mapping[str, Any],
    *,
    validate: bool = True,
) -> Tuple[Path, bool]:
    """
    Write one Item into ``directory`` if its content changed.

    :returns: ``(path, written)`` -- ``written`` is ``False`` when the file on
        disk already held exactly this document.
    """
    identifier = item.get('id')
    if not identifier:
        raise CatalogError('an Item with no id cannot be written')
    check_dated(item)
    if validate:
        validate_item(item)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f'{identifier}.json'
    return path, _write_if_changed(path, item)


def write_items(
    directory: PathLike,
    items: Iterable[Mapping[str, Any]],
    *,
    validate: bool = True,
) -> List[Path]:
    """Write many Items, returning only the paths that actually changed."""
    changed = []
    for item in items:
        path, written = write_item(directory, item, validate=validate)
        if written:
            changed.append(path)
    return changed


def write_collection(
    directory: PathLike,
    collection: Mapping[str, Any],
    *,
    validate: bool = True,
) -> Tuple[Path, bool]:
    """Write ``collection.json`` into ``directory`` if its content changed."""
    if validate:
        validate_collection(collection)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = layout.collection_path(directory)
    return path, _write_if_changed(path, collection)


#: JSON files a catalog directory legitimately holds that are NOT Items: the
#: Collection itself, and the coverage manifest the C++ writers leave in a
#: layer directory (``marine_tiled_raster_store``'s ``coverage.json``).
NON_ITEM_FILENAMES = frozenset({
    layout.COLLECTION_FILENAME,
    layout.COVERAGE_MANIFEST_FILENAME,
})

#: Top-level fields every STAC Item carries (STAC 1.0.0 Item spec, "Item
#: fields": ``bbox`` is required only beside a non-null geometry).
ITEM_REQUIRED_FIELDS = (
    'type', 'stac_version', 'id', 'geometry', 'links', 'assets',
    'properties')


def item_shape_problem(document: Any) -> Optional[str]:
    """
    Say why ``document`` is not a STAC Item, or ``None`` when it is one.

    A structural check, not the full schema validation :func:`validate_item`
    does: it is what a READER applies to every ``.json`` it finds, so that a
    sidecar or a stray document is never enumerated as a product.
    """
    if not isinstance(document, Mapping):
        return f'a JSON {type(document).__name__}, not an object'
    if document.get('type') != 'Feature':
        return f'"type" is {document.get("type")!r}, not "Feature"'
    missing = [name for name in ITEM_REQUIRED_FIELDS if name not in document]
    if missing:
        return f'missing Item field(s) {missing}'
    if not isinstance(document.get('id'), str) or not document['id']:
        return '"id" is not a non-empty string'
    if not isinstance(document.get('properties'), Mapping):
        return '"properties" is not an object'
    return None


def read_items(directory: PathLike) -> List[Dict[str, Any]]:
    """
    Every Item document in ``directory``, ordered by filename.

    ``collection.json`` and ``coverage.json`` are not Items and are skipped;
    a file that is not JSON at all, or is JSON but not a STAC Item, raises,
    because an unreadable Item is a product the catalog would silently stop
    enumerating (Part 2 line 1), and a non-Item admitted as one breaks every
    consumer of the list.
    """
    return [item for _, item in read_item_files(directory)]


def read_item_files(directory: PathLike) -> List[Tuple[Path, Dict[str, Any]]]:
    """
    :func:`read_items`, with the file each Item was read from.

    A caller that removes an Item removes THAT file -- never one named from
    the document's ``id``, which is data: an id holding ``../`` would name a
    file outside the layer, and one that disagrees with its filename would
    name the wrong file.

    Only STAC Items are admitted. The known non-Item files
    (:data:`NON_ITEM_FILENAMES`) are skipped; any other ``.json`` that is not
    an Item is refused by path, never skipped: nothing else is written into a
    catalog directory, so one that appears is either a damaged Item or a file
    in the wrong place, and both need a person.

    :raises CatalogError: naming the file.
    """
    directory = Path(directory)
    if not directory.is_dir():
        return []
    items = []
    for path in sorted(directory.glob('*.json')):
        if path.name in NON_ITEM_FILENAMES:
            continue
        try:
            document = json.loads(path.read_text())
        except ValueError as exc:
            raise CatalogError(f'{path}: not valid JSON: {exc}') from exc
        problem = item_shape_problem(document)
        if problem is not None:
            raise CatalogError(
                f'{path}: not a STAC Item ({problem}). Only Items, '
                f'{sorted(NON_ITEM_FILENAMES)} belong in a catalog '
                'directory; move or remove it, then regenerate')
        items.append((path, document))
    return items


def regenerate_collection(
    directory: PathLike,
    *,
    quantity,
    state,
    origin,
    description: str,
    validate: bool = True,
) -> Tuple[Path, bool, Sequence[Dict[str, Any]]]:
    """
    Rebuild a cell's Collection from the Items already written there.

    :returns: ``(path, written, items)``; ``written`` is ``False`` when the
        Collection was already exactly this.
    """
    files = read_item_files(directory)
    items = [item for _, item in files]
    collection = item_schema.build_collection(
        quantity=quantity, state=state, origin=origin,
        description=description, items=items,
        item_hrefs=[f'./{path.name}' for path, _ in files])
    path, written = write_collection(directory, collection, validate=validate)
    return path, written, items


def _write_if_changed(path: Path, document: Mapping[str, Any]) -> bool:
    """Publish ``document`` as ``path`` unless it is already there."""
    text = json.dumps(document, indent=2, sort_keys=True) + '\n'
    if path.exists():
        try:
            existing = json.loads(path.read_text())
        except ValueError:
            existing = None
        if existing is not None and \
                canonical_json(existing) == canonical_json(document):
            return False
    atomic_io.write_text(path, text)
    return True
