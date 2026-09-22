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
The Item documents the consumer contract promises (design draft Part 2).

Plain dictionaries, built and validated without ``pystac``, so that the schema
is testable on a host where the library is not installed and so that a reader
of the store needs no particular library either (Part 2 line 7: the Items are
JSON). :mod:`marine_world_store.stac_catalog` is the half that validates them
against the STAC spec and writes them.

**Namespacing.** The contract names its fields ``state``, ``origin``,
``fingerprint`` and so on; STAC requires fields outside common metadata to be
prefixed, so they are written as ``mws:state``, ``mws:origin``, ... The
prefix is the spelling, not a second vocabulary -- :data:`CONTRACT_FIELDS`
maps one to the other, and a proposed rev-3 note records the spelling.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence

from marine_world_store import fingerprint as fingerprint_module
from marine_world_store.layout import Origin, Quantity, State

#: STAC version these documents declare.
STAC_VERSION = '1.0.0'

#: Property prefix for everything this store adds to an Item.
PREFIX = 'mws'

#: The store frame (design section 4, spine 0): ITRF2020, ellipsoidal heights.
#: EPSG:9989 is ITRF2020's *geographic 3D* CRS (latitude, longitude,
#: ellipsoidal height) -- verified against this host's PROJ database on
#: 2026-09-22, which is Part 4's "reference-frame EPSG codes verified in PROJ
#: before they are written". 9988 is the geocentric form and 9990 the 2D one;
#: neither carries the height axis the store stores.
STORE_FRAME_NAME = 'ITRF2020'
STORE_EPSG = 9989

#: The *coordinate* epoch the collection is held at. Distinct from ITRF2020's
#: own frame epoch (2015.0, as PROJ reports it): section 4's "reference epoch
#: 2020.0" is the epoch coordinates are propagated to.
STORE_COORDINATE_EPOCH = 2020.0

#: Contract field -> the property name it is written as.
CONTRACT_FIELDS = {
    'quantity': f'{PREFIX}:quantity',
    'state': f'{PREFIX}:state',
    'origin': f'{PREFIX}:origin',
    'frame': f'{PREFIX}:frame',
    'inputs': f'{PREFIX}:inputs',
    'fingerprint': f'{PREFIX}:fingerprint',
    'uncertainty_basis': f'{PREFIX}:uncertainty_basis',
    'resolution_m': f'{PREFIX}:resolution_m',
    'levels': f'{PREFIX}:levels',
    'cell_fields': f'{PREFIX}:cell_fields',
    'geometric_error_m': f'{PREFIX}:geometric_error_m',
    'sigma_fold': f'{PREFIX}:sigma_fold',
    'tile': f'{PREFIX}:tile',
    'source': f'{PREFIX}:source',
    'revision': f'{PREFIX}:revision',
}

#: Fields Part 2 line 2 requires of *every* product Item.
REQUIRED_CONTRACT_FIELDS = (
    'quantity', 'state', 'origin', 'frame', 'inputs', 'fingerprint',
    'uncertainty_basis', 'resolution_m', 'levels',
)

#: Media type of a Cloud-Optimized GeoTIFF asset.
COG_MEDIA_TYPE = 'image/tiff; application=geotiff; profile=cloud-optimized'


class ItemSchemaError(ValueError):
    """An Item that would not keep the consumer contract."""


@dataclass(frozen=True)
class CellField:
    """One per-cell field of a field-quantity tile (Part 2 line 3)."""

    name: str
    band: int
    description: str
    units: Optional[str] = None

    def to_dict(self) -> Dict[str, Any]:
        """Spell this field the way the Item carries it."""
        return {k: v for k, v in asdict(self).items() if v is not None}


def store_frame() -> Dict[str, Any]:
    """Build the frame declaration every Item carries (never bare 4326)."""
    return {
        'name': STORE_FRAME_NAME,
        'epsg': STORE_EPSG,
        'coordinate_epoch': STORE_COORDINATE_EPOCH,
        'height': 'ellipsoidal',
    }


def depth_cell_fields(bands: int = 2) -> List[CellField]:
    """
    Per-cell fields of a native depth tile as this repo writes them.

    ``marine_bathymetry_store`` writes a 2-band Float64 GeoTIFF (value,
    uncertainty). The contract's ``contributor`` and measured-vs-interpolated
    fields are adopted in the design but are **not** in that container, so they
    are not claimed here: an Item that listed fields the tile does not hold
    would be a promise the store cannot keep.
    """
    if bands != 2:
        raise ItemSchemaError(
            f'depth tiles in this repo are 2-band (value, sigma); got {bands}. '
            'The 4-band MIN/MEAN/COUNT/sigma overview schema is Group B.')
    return [
        CellField('value', 1, 'Ellipsoidal height of the seafloor', 'm'),
        CellField('sigma', 2, 'Per-cell uncertainty (1 sigma)', 'm'),
    ]


def collection_id(quantity, state, origin) -> str:
    """Stable id of the Collection a quantity/state/origin cell holds."""
    return '-'.join((
        Quantity(quantity).value, State(state).value, Origin(origin).value))


def tile_item_id(quantity, state, origin, level: int, row: int,
                 col: int) -> str:
    """Stable, content-independent Item id for one tile."""
    return f'{collection_id(quantity, state, origin)}-{level}_{row}_{col}'


def build_tile_item(
    *,
    quantity,
    state,
    origin,
    level: int,
    row: int,
    col: int,
    asset_href: str,
    fingerprint_inputs: Mapping[str, Any],
    uncertainty_basis: str,
    resolution_m: Optional[float] = None,
    levels: Optional[Sequence[int]] = None,
    cell_fields: Optional[Iterable[CellField]] = None,
    geometry: Optional[Mapping[str, Any]] = None,
    bbox: Optional[Sequence[float]] = None,
    start_datetime: Optional[str] = None,
    end_datetime: Optional[str] = None,
    license_id: str = 'CC0-1.0',
    geometric_error_m: Optional[float] = None,
    sigma_fold: Optional[str] = None,
    frame: Optional[Mapping[str, Any]] = None,
    extra_properties: Optional[Mapping[str, Any]] = None,
) -> Dict[str, Any]:
    """
    Build the Item for one tile of a field quantity store.

    :param fingerprint_inputs: the inputs of
        :func:`marine_world_store.fingerprint.fingerprint_document`; both the
        hash and the inputs are written, so a consumer that disagrees can see
        *which* input it disagrees about.
    :param geometric_error_m: uma-ADR-0013 D1's per-tile error, read from the
        producer's coverage manifest. Recording it on the Item is what lets the
        D7 selection core (uma#395) treat a rev-3 overview like any other tile;
        ``None`` means the producer recorded none, and is never written as 0.
    :param sigma_fold: which sigma-fold rule produced an overview's sigma band.
        The rule is **open** (design section 7 as amended); Group B writes
        ``"undecided"`` here rather than a sigma band nobody agreed on.
    :param frame: override for the frame declaration. Defaults to the store
        frame, which is what a product built by the store's own link step
        carries. A product that is a byte-identical re-expression of an
        existing tile has **not** been transformed, and must declare the frame
        it actually holds plus the owed transformation -- an Item that named
        the store frame there would be a false claim in the record.
    """
    document = fingerprint_module.fingerprint_document(**dict(
        fingerprint_inputs))
    properties: Dict[str, Any] = {
        'datetime': start_datetime if end_datetime is None else None,
        'license': license_id,
        'proj:epsg': STORE_EPSG,
        CONTRACT_FIELDS['quantity']: Quantity(quantity).value,
        CONTRACT_FIELDS['state']: State(state).value,
        CONTRACT_FIELDS['origin']: Origin(origin).value,
        CONTRACT_FIELDS['frame']: dict(frame) if frame else store_frame(),
        CONTRACT_FIELDS['inputs']: document,
        CONTRACT_FIELDS['fingerprint']:
            fingerprint_module.fingerprint_of(document),
        CONTRACT_FIELDS['uncertainty_basis']: uncertainty_basis,
        CONTRACT_FIELDS['resolution_m']: resolution_m,
        CONTRACT_FIELDS['levels']: list(levels) if levels else [level],
        CONTRACT_FIELDS['tile']: {'level': level, 'row': row, 'col': col},
    }
    if start_datetime and end_datetime:
        properties['start_datetime'] = start_datetime
        properties['end_datetime'] = end_datetime
    if cell_fields is not None:
        properties[CONTRACT_FIELDS['cell_fields']] = [
            f.to_dict() for f in cell_fields]
    if geometric_error_m is not None:
        properties[CONTRACT_FIELDS['geometric_error_m']] = float(
            geometric_error_m)
    if sigma_fold is not None:
        properties[CONTRACT_FIELDS['sigma_fold']] = sigma_fold
    if extra_properties:
        properties.update(extra_properties)

    item = {
        'type': 'Feature',
        'stac_version': STAC_VERSION,
        'id': tile_item_id(quantity, state, origin, level, row, col),
        'collection': collection_id(quantity, state, origin),
        'geometry': dict(geometry) if geometry else None,
        'properties': properties,
        'links': [],
        'assets': {
            'data': {
                'href': asset_href,
                'type': COG_MEDIA_TYPE,
                'roles': ['data'],
            },
        },
    }
    if bbox is not None:
        if item['geometry'] is None:
            raise ItemSchemaError(
                'a bbox without a geometry is not a STAC Item; pass both or '
                'neither')
        item['bbox'] = [float(v) for v in bbox]
    validate_contract(item)
    return item


def build_collection(
    *,
    quantity,
    state,
    origin,
    description: str,
    items: Sequence[Mapping[str, Any]],
    license_id: str = 'CC0-1.0',
) -> Dict[str, Any]:
    """Build the Collection over ``items`` (Part 2 line 1: no globbing)."""
    bboxes = [item['bbox'] for item in items if item.get('bbox')]
    if bboxes:
        spatial = [[
            min(b[0] for b in bboxes), min(b[1] for b in bboxes),
            max(b[2] for b in bboxes), max(b[3] for b in bboxes)]]
    else:
        # A Collection must declare an extent; unknown is the whole world,
        # which is honest, where a zero-size box would be a false claim.
        spatial = [[-180.0, -90.0, 180.0, 90.0]]
    intervals = [
        [item['properties'].get('start_datetime'),
         item['properties'].get('end_datetime')]
        for item in items
        if item['properties'].get('start_datetime')]
    return {
        'type': 'Collection',
        'stac_version': STAC_VERSION,
        'id': collection_id(quantity, state, origin),
        'description': description,
        'license': license_id,
        'extent': {
            'spatial': {'bbox': spatial},
            'temporal': {'interval': intervals or [[None, None]]},
        },
        'links': [],
        'properties': {
            CONTRACT_FIELDS['quantity']: Quantity(quantity).value,
            CONTRACT_FIELDS['state']: State(state).value,
            CONTRACT_FIELDS['origin']: Origin(origin).value,
            CONTRACT_FIELDS['frame']: store_frame(),
        },
        'summaries': {
            CONTRACT_FIELDS['levels']: sorted({
                lvl for item in items
                for lvl in item['properties'].get(
                    CONTRACT_FIELDS['levels'], [])}),
        },
    }


def validate_contract(item: Mapping[str, Any]) -> None:
    """
    Fail on an Item missing a field Part 2 line 2 promises a consumer.

    ``resolution_m`` may be present and null -- a producer that does not know a
    tile's ground resolution says so rather than inventing one -- but the key
    itself must be there, so the absence is a statement rather than a gap.

    :raises ItemSchemaError: naming every missing field at once; a writer
        fixing them one at a time learns the schema one round trip at a time.
    """
    properties = item.get('properties') or {}
    absent = [name for name in REQUIRED_CONTRACT_FIELDS
              if CONTRACT_FIELDS[name] not in properties]
    empty = [name for name in REQUIRED_CONTRACT_FIELDS
             if name != 'resolution_m'
             and CONTRACT_FIELDS[name] in properties
             and properties[CONTRACT_FIELDS[name]] in (None, '', [], {})]
    missing = sorted(set(absent + empty))
    if missing:
        raise ItemSchemaError(
            f'Item {item.get("id")!r} is missing contract field(s): '
            f'{missing}')


def build_source_item(
    *,
    source_id: str,
    kind: str,
    name: str,
    file_keys: Optional[Sequence[str]] = None,
    role: str = 'data',
    platform: Optional[str] = None,
    recorder: Optional[str] = None,
    start_datetime: Optional[str] = None,
    end_datetime: Optional[str] = None,
    frame: Optional[Mapping[str, Any]] = None,
    href: Optional[str] = None,
    license_id: str = 'CC0-1.0',
    notes: Optional[str] = None,
) -> Dict[str, Any]:
    r"""
    Build the ``sources/`` Item for one imported source.

    :param source_id: the content id from
        :mod:`marine_world_store.source_identity`.
    :param file_keys: the ``<filename>\\t<file key>`` lines the id was taken
        over, so the id is checkable against the files without recomputing it.
    :param platform: lookup metadata, **never** identity (design section 3) --
        which is why it sits beside the id rather than inside it.
    :param role: ``data`` or ``engineering``; engineering material is indexed
        beside the sources and is never an input (design section 2).
    :param frame: the source's own frame and epoch, where it is known. The
        store frame is what a *product* declares; a source declares what it
        arrived in, and the transformation into the store frame is named in
        the product's provenance.
    """
    if not source_id or not str(source_id).strip():
        raise ItemSchemaError('a source Item needs its content id')
    if role not in ('data', 'engineering'):
        raise ItemSchemaError(
            f'role must be "data" or "engineering", got {role!r}')
    properties: Dict[str, Any] = {
        'datetime': start_datetime,
        'license': license_id,
        CONTRACT_FIELDS['source']: {
            'id': source_id,
            'kind': kind,
            'name': name,
            'role': role,
            'file_keys': list(file_keys or []),
        },
    }
    for key, value in (('platform', platform), ('recorder', recorder)):
        if value:
            properties[f'{PREFIX}:{key}'] = value
    if start_datetime and end_datetime:
        properties['start_datetime'] = start_datetime
        properties['end_datetime'] = end_datetime
        properties['datetime'] = None
    if frame:
        properties[CONTRACT_FIELDS['frame']] = dict(frame)
    if notes:
        properties[f'{PREFIX}:notes'] = notes
    item = {
        'type': 'Feature',
        'stac_version': STAC_VERSION,
        'id': source_id,
        'collection': 'sources',
        'geometry': None,
        'properties': properties,
        'links': [],
        'assets': {},
    }
    if href:
        item['assets']['source'] = {
            'href': href,
            'roles': ['data' if role == 'data' else 'metadata'],
        }
    return item
