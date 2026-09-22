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

**Only changed Items are rewritten** (design section 9's replica rule: "write
only changed Items"). A rewrite is gated on the canonical-JSON content hash of
the document, not on an mtime, so regenerating an unchanged store touches
nothing and ``rclone bisync`` moves nothing.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, Sequence, Tuple, Union
import warnings

from marine_world_store import item_schema, layout
from marine_world_store.fingerprint import canonical_json
import pystac

PathLike = Union[str, Path]


class CatalogError(RuntimeError):
    """An Item or Collection that could not be written as specified."""


class ValidatorUnavailable(UserWarning):
    """The STAC validator could not run, so nothing was checked."""


def _validate(document: Mapping[str, Any], stac_type) -> None:
    """
    Validate one document, distinguishing *invalid* from *unchecked*.

    Three outcomes, and they are not the same thing:

    * the document is not a STAC object at all, or fails the schema -- an
      error, because a malformed Item must not enter the store;
    * the validator itself cannot run (no ``jsonschema``, or the remote schema
      is unreachable -- a boat is offline by default) -- a warning, and the
      write proceeds. A validator that could not run reports no verdict; the
      alternative is refusing to record data because a schema host is down.
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
        warnings.warn(
            f'STAC validation skipped for {document.get("id")!r}: {exc}',
            ValidatorUnavailable, stacklevel=3)


def validate_item(item: Mapping[str, Any]) -> None:
    """Validate ``item`` against the STAC spec (see :func:`_validate`)."""
    _validate(item, pystac.Item)


def validate_collection(collection: Mapping[str, Any]) -> None:
    """Validate ``collection`` against the STAC spec."""
    _validate(collection, pystac.Collection)


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


def read_items(directory: PathLike) -> List[Dict[str, Any]]:
    """
    Every Item document in ``directory``, ordered by id.

    ``collection.json`` is not an Item and is skipped; a file that is not JSON
    at all raises, because an unreadable Item is a product the catalog would
    silently stop enumerating (Part 2 line 1).
    """
    directory = Path(directory)
    if not directory.is_dir():
        return []
    items = []
    for path in sorted(directory.glob('*.json')):
        if path.name == layout.COLLECTION_FILENAME:
            continue
        try:
            items.append(json.loads(path.read_text()))
        except ValueError as exc:
            raise CatalogError(f'{path}: not valid JSON: {exc}') from exc
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
    items = read_items(directory)
    collection = item_schema.build_collection(
        quantity=quantity, state=state, origin=origin,
        description=description, items=items)
    path, written = write_collection(directory, collection, validate=validate)
    return path, written, items


def _write_if_changed(path: Path, document: Mapping[str, Any]) -> bool:
    """tmp-then-rename ``document`` into ``path`` unless it is already there."""
    text = json.dumps(document, indent=2, sort_keys=True) + '\n'
    if path.exists():
        try:
            existing = json.loads(path.read_text())
        except ValueError:
            existing = None
        if existing is not None and \
                canonical_json(existing) == canonical_json(document):
            return False
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(text)
    tmp.replace(path)
    return True
