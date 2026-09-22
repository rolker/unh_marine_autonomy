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
Re-express existing depth tiles under the rev-3 tree -- an *adapter*.

This is not a builder. It does not re-link soundings, and there is no
observation or trajectory stage behind it: it takes the COG tiles
``marine_bathymetry_store`` already wrote, copies them **byte-identical** into
``<root>/depths/<state>/<origin>/``, and gives each one the rev-3 record --
a ``sources/`` Item with the Merkle id of the bag it came from, a fingerprint
over the inputs that are actually known, and the per-tile ``geometric_error_m``
read from the producer's own coverage manifest.

What that demonstrates is the layout, the identity and the Items end to end on
real data. What it deliberately does **not** claim:

* **No reframing.** A byte-identical copy has not been transformed into the
  store frame, so the Item declares the georeferencing the tile actually
  carries and names the transformation as owed. Declaring ITRF2020 over tiles
  that were written in WGS84 would put a false claim in the record.
* **No re-link.** The datum and geometry records in ``revisions/`` are applied
  at link; this adapter does not link, so a record that would change these
  cells is listed in the fingerprint inputs only when the caller names it.

The existing store is opened read-only and is never modified.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
from pathlib import Path
import shutil
from typing import Any, Dict, List, Mapping, Optional, Sequence, Union

from marine_world_store import coverage, footprint, item_schema, layout
from marine_world_store.layout import Origin, Quantity, State

PathLike = Union[str, Path]

#: What the copied tiles' per-cell sigma means. Stated rather than invented:
#: the value is whatever the producing ingest wrote, and this adapter does not
#: recompute it.
DEFAULT_UNCERTAINTY_BASIS = (
    'per-cell 1-sigma as written by marine_bathymetry_store; not recomputed '
    'by this adapter')

#: The frame a copied tile actually carries, with the owed work named.
LEGACY_FRAME = {
    'name': 'WGS84 as written by marine_bathymetry_store',
    'epsg': 4326,
    'height': 'ellipsoidal',
    'transformation_to_store_frame': (
        'not applied: these tiles are a byte-identical re-expression of '
        'existing products, not a link. The transformation into the store '
        'frame (ITRF2020 at epoch 2020.0, EPSG:9989) is owed, and the datum '
        'records that carry it live in revisions/.'),
}


class AdapterError(RuntimeError):
    """The subset could not be adapted as specified."""


@dataclass
class AdaptReport:
    """What one adapter run did, in numbers a caller can assert on."""

    tiles_seen: int = 0
    tiles_copied: int = 0
    tiles_unchanged: int = 0
    items_written: int = 0
    items_unchanged: int = 0
    missing_geometric_error: int = 0
    destination: Optional[Path] = None
    items: List[Dict[str, Any]] = field(default_factory=list)


def file_sha256(path: PathLike) -> str:
    """sha256 of a file's bytes -- the byte-identical check."""
    digest = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def adapt_depth_tiles(
    *,
    source_layer_dir: PathLike,
    root: PathLike,
    source_ids: Sequence[str],
    builder_version: str,
    state: Union[State, str] = State.REVIEWED,
    origin: Union[Origin, str] = Origin.SURVEYED,
    revision_ids: Optional[Sequence[str]] = None,
    levels: Optional[Sequence[int]] = None,
    uncertainty_basis: str = DEFAULT_UNCERTAINTY_BASIS,
    start_datetime: Optional[str] = None,
    end_datetime: Optional[str] = None,
    dry_run: bool = False,
) -> AdaptReport:
    """
    Copy ``source_layer_dir``'s tiles under the rev-3 tree with Items.

    :param source_layer_dir: an existing layer directory, e.g.
        ``<store>/processed``. Read-only.
    :param source_ids: the content ids of the bags these tiles came from.
        Required, and required to be non-empty: a product whose inputs are
        unrecorded has no fingerprint worth writing.
    :param levels: when given, only tiles at these levels are adapted.
    :raises AdapterError: when the source layer holds no tile, or a copy does
        not compare byte-identical.
    """
    source_layer_dir = Path(source_layer_dir)
    if not source_layer_dir.is_dir():
        raise AdapterError(f'{source_layer_dir}: not a layer directory')
    if not source_ids:
        raise AdapterError(
            'name the source bag id(s) these tiles were built from; a product '
            'with no recorded inputs cannot be fingerprinted')

    tiles = layout.tiles_in_dir(source_layer_dir)
    if levels is not None:
        wanted = set(levels)
        tiles = [t for t in tiles
                 if layout.parse_tile_filename(t.name)[0] in wanted]
    if not tiles:
        raise AdapterError(
            f'{source_layer_dir}: no tile files'
            + (f' at level(s) {sorted(set(levels))}' if levels else ''))

    manifest = coverage.coverage_for_layer(source_layer_dir)
    destination = layout.quantity_dir(root, Quantity.DEPTHS, state, origin)
    report = AdaptReport(destination=destination)
    if not dry_run:
        destination.mkdir(parents=True, exist_ok=True)

    for tile in tiles:
        key = layout.parse_tile_filename(tile.name)
        level, row, col = key
        report.tiles_seen += 1
        target = destination / tile.name
        if not dry_run:
            if _copy_identical(tile, target):
                report.tiles_copied += 1
            else:
                report.tiles_unchanged += 1
        geometric_error = manifest.geometric_error(key)
        if geometric_error is None:
            report.missing_geometric_error += 1
        fingerprint_inputs: Dict[str, Any] = {
            'source_ids': list(source_ids),
            'builder_version': builder_version,
        }
        if revision_ids:
            fingerprint_inputs['revision_ids'] = list(revision_ids)
        geometry, bbox = footprint.tile_footprint(tile)
        item = item_schema.build_tile_item(
            quantity=Quantity.DEPTHS,
            state=state,
            origin=origin,
            level=level, row=row, col=col,
            asset_href=f'./{tile.name}',
            fingerprint_inputs=fingerprint_inputs,
            uncertainty_basis=uncertainty_basis,
            resolution_m=footprint.cell_size_m(tile),
            levels=[level],
            cell_fields=item_schema.depth_cell_fields(),
            geometry=geometry,
            bbox=bbox,
            start_datetime=start_datetime,
            end_datetime=end_datetime,
            geometric_error_m=geometric_error,
            frame=LEGACY_FRAME,
            extra_properties={
                f'{item_schema.PREFIX}:adapted_from': str(tile),
                f'{item_schema.PREFIX}:notes': (
                    'byte-identical re-expression of an existing '
                    'marine_bathymetry_store tile; not a link'),
            },
        )
        report.items.append(item)

    return report


def write_report_items(report: AdaptReport, *, validate: bool = True
                       ) -> AdaptReport:
    """
    Write a report's Items and its cell Collection (needs ``pystac``).

    Separated from :func:`adapt_depth_tiles` so the adapter itself -- the
    copying, the identity, the geometric error -- is testable on a host where
    ``pystac`` has not been installed yet.
    """
    from marine_world_store import stac_catalog
    if report.destination is None:
        raise AdapterError('report carries no destination')
    changed = stac_catalog.write_items(
        report.destination, report.items, validate=validate)
    report.items_written = len(changed)
    report.items_unchanged = len(report.items) - len(changed)
    first = report.items[0]['properties'] if report.items else {}
    stac_catalog.regenerate_collection(
        report.destination,
        quantity=first.get(item_schema.CONTRACT_FIELDS['quantity'],
                           Quantity.DEPTHS.value),
        state=first.get(item_schema.CONTRACT_FIELDS['state'],
                        State.REVIEWED.value),
        origin=first.get(item_schema.CONTRACT_FIELDS['origin'],
                         Origin.SURVEYED.value),
        description='depth tiles adapted from the existing store (#397)',
        validate=validate)
    return report


def _copy_identical(source: Path, target: Path) -> bool:
    """
    Copy ``source`` to ``target`` unless it is already there, byte for byte.

    :returns: ``True`` when the file was written.
    :raises AdapterError: when the copy does not compare equal -- the one
        promise this adapter makes about the pixels is that it changed none.
    """
    digest = file_sha256(source)
    if target.exists() and file_sha256(target) == digest:
        return False
    tmp = target.with_suffix(target.suffix + '.tmp')
    shutil.copy2(source, tmp)
    if file_sha256(tmp) != digest:
        tmp.unlink(missing_ok=True)
        raise AdapterError(
            f'{source} -> {target}: copy is not byte-identical')
    tmp.replace(target)
    return True


def sources_from_manifest(document: Mapping[str, Any]) -> List[Dict[str, Any]]:
    """
    Read the ``sources:`` list of a subset manifest.

    The subset's bag paths are **inputs**, never literals in code: they name
    read-only material on a particular host (or the NAS), and a path baked into
    a module would be wrong on the next one.
    """
    sources = document.get('sources')
    if not isinstance(sources, list) or not sources:
        raise AdapterError(
            'the subset manifest needs a non-empty sources: list, each entry '
            'with a path: to the bag directory or file')
    entries = []
    for entry in sources:
        if isinstance(entry, str):
            entry = {'path': entry}
        if not isinstance(entry, Mapping) or not entry.get('path'):
            raise AdapterError(f'sources entry has no path: {entry!r}')
        entries.append(dict(entry))
    return entries
