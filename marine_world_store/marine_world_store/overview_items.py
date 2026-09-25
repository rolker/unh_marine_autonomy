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
STAC Items for the derived overview tiles of one quantity layer.

The per-parent writer (``build_depth_overview_parent``) leaves beside each
derived tile a ``<level>_<row>_<col>.json`` record -- its geometric error, its
sigma rule, and the CHILDREN it folded. This module turns those records into
the Items the consumer contract promises for every product (design draft
Part 2): an overview tile is found through the Collection like any other tile,
and carries ``geometric_error_m`` and ``sigma_fold`` where the D7 selection
core and a reader expect them.

Nothing about an overview is invented here; each field comes from its lineage:

* **the observation interval** is the union of its children's -- ultimately of
  the native tiles' Items, which are dated from their source bags. A tile whose
  lineage cannot be dated is refused, like every other undated Item;
* **the fingerprint inputs** are the union of its children's sources and
  revisions (what a search by source finds), plus a builder version naming
  the fold, its sigma rule, and every child by tile AND complete fingerprint.
  The child fingerprints are what make the parent's honest: a child whose
  trajectory, geometry revision, decoder, cache method or consumer ordering
  changed has a new fingerprint, and so therefore does every ancestor -- a
  changed rule or a re-linked child is a new fingerprint rather than a silent
  migration;
* **the frame** is its children's, which must agree;
* **the footprint** is read from the tile itself (:mod:`marine_world_store.
  footprint`), as for a native tile.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Mapping, Sequence, Tuple, Union

from marine_world_store import fingerprint as fingerprint_module
from marine_world_store import footprint, item_schema, layout
from marine_world_store import overview_records, source_time
from marine_world_store.item_schema import CONTRACT_FIELDS

PathLike = Union[str, Path]

#: The fold that writes the tiles, as its builder version records it.
OVERVIEW_BUILDER = 'build_depth_overview_parent (depth-overview-tile/1)'

#: Marks an overview Item, so a regenerate can tell it from a native one.
OVERVIEW_FLAG = f'{item_schema.PREFIX}:overview'

#: The children a tile folded, as its record names them.
CHILDREN_FIELD = f'{item_schema.PREFIX}:children'


class OverviewItemError(ValueError):
    """An overview tile whose Item cannot be built truthfully."""


def is_overview_item(item: Mapping[str, Any]) -> bool:
    """Whether ``item`` describes a derived overview tile."""
    return bool((item.get('properties') or {}).get(OVERVIEW_FLAG))


def _tile_key(item: Mapping[str, Any]) -> Tuple[int, int, int]:
    tile = (item.get('properties') or {}).get(CONTRACT_FIELDS['tile']) or {}
    return (tile.get('level'), tile.get('row'), tile.get('col'))


def build_overview_items(
    layer_dir: PathLike,
    *,
    quantity,
    state,
    origin,
    existing_items: Sequence[Mapping[str, Any]],
) -> List[Dict[str, Any]]:
    """
    Build the Item of every derived tile in ``<layer_dir>/overviews/``.

    :param existing_items: the Items already in ``layer_dir`` (the native
        tiles'; overview Items among them are ignored and rebuilt).
    :returns: the overview Items, finest level first.
    :raises OverviewItemError: naming every tile whose Item could not be
        built -- no record, a record that names no children, a child with no
        Item, children in different frames, or an undatable lineage. Nothing
        is returned for any of them: a partial set would hide the rest.
    """
    layer_dir = Path(layer_dir)
    overviews = layout.overviews_dir(layer_dir)
    records = overview_records.read_tile_records(overviews)
    by_path: Dict[str, Mapping[str, Any]] = {}
    for item in existing_items:
        if is_overview_item(item):
            continue
        level, row, col = _tile_key(item)
        if level is None:
            continue
        by_path[layout.tile_filename(level, row, col)] = item

    tiles = sorted(
        ((layout.parse_tile_filename(t.name), t)
         for t in layout.tiles_in_dir(overviews)),
        key=lambda pair: (-pair[0][0], pair[0][1], pair[0][2]))
    built: List[Dict[str, Any]] = []
    problems: List[str] = []
    for key, tile in tiles:
        name = tile.name
        record = records.get(key)
        if record is None or not record.children:
            problems.append(
                f'{name}: no per-tile record naming its children, so its '
                'lineage -- and therefore its time -- is unknown. Only the '
                'per-parent writer (build_depth_overview_parent) records '
                'children; rebuild the tile with it -- the regenerate '
                'workflow rebuilds any tile whose record is missing, so '
                f'remove {Path(name).with_suffix(".json").name} (or all of '
                'overviews/) and rerun it')
            continue
        children = [by_path.get(child) for child in record.children]
        missing = [child for child, item in zip(record.children, children)
                   if item is None]
        if missing:
            problems.append(f'{name}: child(ren) with no Item: {missing}')
            continue
        try:
            item = _overview_item(
                tile, key, record, children,
                quantity=quantity, state=state, origin=origin)
        except (OverviewItemError, item_schema.ItemSchemaError,
                source_time.TimeIntervalError,
                footprint.FootprintError) as exc:
            problems.append(f'{name}: {exc}')
            continue
        built.append(item)
        by_path[f'{layout.OVERVIEWS_DIR}/{name}'] = item
    if problems:
        raise OverviewItemError(
            f'{len(problems)} overview tile(s) under {overviews} cannot be '
            'given an Item:\n  ' + '\n  '.join(problems))
    return built


def overview_builder_version(sigma_fold: str, child_names: Sequence[str],
                             child_inputs: Sequence[Mapping[str, Any]]) -> str:
    """
    Name the fold that built an overview tile, and exactly what it folded.

    The fold version (:data:`OVERVIEW_BUILDER` and the sigma rule), then each
    child as ``<tile>=<fingerprint>``, sorted by tile. The fingerprint is
    recomputed from the child's COMPLETE inputs document rather than read from
    the child's Item, so every input section 9 names -- not only the sources,
    revisions and builder a union can carry -- reaches the parent, and a
    child Item whose stored hash disagrees with its inputs cannot vouch for
    itself. The tile identity is there because the same product placed under
    a different parent cell is a different fold.

    Carried in ``builder_version`` because section 9's input set is closed
    (:data:`marine_world_store.fingerprint.FINGERPRINT_KEYS`): an overview's
    process IS "this fold over these children", so that is what it names.
    """
    if len(child_names) != len(child_inputs):
        raise OverviewItemError(
            f'{len(child_names)} child name(s) for {len(child_inputs)} '
            'child input document(s)')
    parts = sorted(
        f'{name}={fingerprint_module.fingerprint_of(document)}'
        for name, document in zip(child_names, child_inputs))
    return (f'{OVERVIEW_BUILDER} sigma_fold={sigma_fold} over '
            f'[{"; ".join(parts)}]')


def _overview_item(tile: Path, key, record, children, *, quantity, state,
                   origin) -> Dict[str, Any]:
    level, row, col = key
    inputs = [c['properties'][CONTRACT_FIELDS['inputs']] for c in children]
    source_ids = sorted({s for doc in inputs
                         for s in doc.get('source_ids', [])})
    revision_ids = sorted({r for doc in inputs
                           for r in doc.get('revision_ids', [])})
    sigma_fold = record.sigma_fold or 'unrecorded'
    fingerprint_inputs: Dict[str, Any] = {
        'builder_version': overview_builder_version(
            sigma_fold, record.children, inputs),
    }
    if source_ids:
        fingerprint_inputs['source_ids'] = source_ids
    if revision_ids:
        fingerprint_inputs['revision_ids'] = revision_ids

    frames = []
    for child in children:
        frame = child['properties'].get(CONTRACT_FIELDS['frame'])
        if frame not in frames:
            frames.append(frame)
    if len(frames) != 1 or frames[0] is None:
        raise OverviewItemError(
            f'its children declare {len(frames)} frame(s); a fold of tiles in '
            'different frames has no single frame to declare')

    start, end = source_time.union_intervals(
        [(c['properties'].get('start_datetime')
          or c['properties'].get('datetime'),
          c['properties'].get('end_datetime')
          or c['properties'].get('datetime')) for c in children],
        what=f'overview tile {tile.name}')
    geometry, bbox = footprint.tile_footprint(tile)
    return item_schema.build_tile_item(
        quantity=quantity, state=state, origin=origin,
        level=level, row=row, col=col,
        asset_href=f'./{layout.OVERVIEWS_DIR}/{tile.name}',
        fingerprint_inputs=fingerprint_inputs,
        uncertainty_basis=(
            'sigma band reserved (nodata): the fold rule is undecided, design '
            'section 7' if sigma_fold == 'undecided' else
            f'sigma folded from the children by rule {sigma_fold!r}'),
        resolution_m=footprint.cell_size_m(tile),
        levels=[level],
        cell_fields=item_schema.depth_overview_cell_fields(sigma_fold),
        geometry=geometry,
        bbox=bbox,
        start_datetime=start,
        end_datetime=end,
        geometric_error_m=record.geometric_error_m,
        sigma_fold=sigma_fold,
        frame=frames[0],
        extra_properties={
            OVERVIEW_FLAG: True,
            CHILDREN_FIELD: list(record.children),
        },
    )
