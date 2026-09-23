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

**Every Item is dated.** Part 2 line 2 promises a time range, and STAC gives
an Item exactly two legal shapes -- one ``datetime``, or a null ``datetime``
with both ends of a range. There is no third shape for "unknown", so an Item
with no derivable interval is a provenance defect rather than an
under-specified document: these builders raise and write nothing. The
interval comes from the sources (:mod:`marine_world_store.source_time`);
a product takes the union of its sources'.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence

from marine_world_store import fingerprint as fingerprint_module
from marine_world_store import source_time
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


def dated(start_datetime: Optional[str], end_datetime: Optional[str], *,
          what: str) -> Dict[str, Any]:
    """
    Spell an Item's observation interval, or refuse to build the Item.

    :returns: the three time properties, in the one legal STAC shape for a
        range: a null ``datetime`` with both ends beside it.
    :raises ItemSchemaError: when either end is missing or unusable. The
        writer stops here rather than emitting ``"datetime": null`` with
        nothing beside it, which is not a STAC Item at all.
    """
    try:
        start, end = source_time.check_interval(
            start_datetime, end_datetime, what=what)
    except source_time.TimeIntervalError as exc:
        raise ItemSchemaError(str(exc)) from exc
    return {'datetime': None, 'start_datetime': start, 'end_datetime': end}


def store_frame() -> Dict[str, Any]:
    """Build the frame declaration every Item carries (never bare 4326)."""
    return {
        'name': STORE_FRAME_NAME,
        'epsg': STORE_EPSG,
        'coordinate_epoch': STORE_COORDINATE_EPOCH,
        'height': 'ellipsoidal',
    }


def frame_epsg(frame: Mapping[str, Any], *, what: str) -> int:
    """
    Return the EPSG code a frame declaration names, or refuse the frame.

    Design section 4: a frame is a *specific* EPSG code, always. A declaration
    without one could not be written to ``proj:epsg`` without either inventing
    a code or silently falling back to the store frame's -- both of which are
    false claims about where the tile's coordinates are.

    :raises ItemSchemaError: when ``frame`` has no integer ``epsg``.
    """
    epsg = frame.get('epsg')
    if not isinstance(epsg, int) or isinstance(epsg, bool) or epsg <= 0:
        raise ItemSchemaError(
            f'{what}: the frame declaration {dict(frame)!r} names no EPSG '
            'code; design section 4 requires a specific one')
    return epsg


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
            f'native depth tiles in this repo are 2-band (value, sigma); got '
            f'{bands}. The 4-band overview schema is '
            'depth_overview_cell_fields().')
    return [
        CellField('value', 1, 'Ellipsoidal height of the seafloor', 'm'),
        CellField('sigma', 2, 'Per-cell uncertainty (1 sigma)', 'm'),
    ]


def depth_overview_cell_fields(sigma_fold: str) -> List[CellField]:
    """
    Per-cell fields of a rev-3 4-band depth OVERVIEW tile (design section 7).

    Band order is ``marine_bathymetry_store``'s ``MultiBandIndex``: MIN, MEAN,
    COUNT, sigma. The sigma band's description says what it holds under
    ``sigma_fold`` -- while the rule is ``undecided`` the band is written as
    nodata, and an Item that described it as an uncertainty would be a promise
    the tile does not keep.
    """
    sigma = ('RESERVED: written as nodata while the sigma fold rule is '
             'undecided (design section 7)' if sigma_fold == 'undecided' else
             f'Per-cell uncertainty (1 sigma), folded by rule {sigma_fold!r}')
    return [
        CellField('min', 1, 'Shoalest ellipsoidal height of the cells this '
                  'cell summarises (navigation reads this band)', 'm'),
        CellField('mean', 2, 'Count-weighted mean ellipsoidal height', 'm'),
        CellField('count', 3, 'How many native cells this cell summarises '
                  '(lineage: the evidence the mean rests on)'),
        CellField('sigma', 4, sigma, 'm'),
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

    :param start_datetime: start of the observation interval the tile's
        material was recorded over, RFC 3339. **Required**, with
        ``end_datetime``: a product nobody can date cannot be found by the
        time search Part 2 line 1 promises. A product takes the union of its
        sources' intervals (:func:`marine_world_store.source_time.
        union_intervals`).
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
    identifier = tile_item_id(quantity, state, origin, level, row, col)
    document = fingerprint_module.fingerprint_document(**dict(
        fingerprint_inputs))
    declared_frame = dict(frame) if frame else store_frame()
    properties: Dict[str, Any] = {
        'license': license_id,
        # The projection extension's code and the frame declaration are the
        # same claim written twice; they must never disagree. Hard-coding the
        # store frame here put EPSG:9989 on tiles an adapter had just declared
        # as untransformed EPSG:4326 -- the false frame claim `frame=` exists
        # to avoid.
        'proj:epsg': frame_epsg(declared_frame, what=identifier),
        CONTRACT_FIELDS['quantity']: Quantity(quantity).value,
        CONTRACT_FIELDS['state']: State(state).value,
        CONTRACT_FIELDS['origin']: Origin(origin).value,
        CONTRACT_FIELDS['frame']: declared_frame,
        CONTRACT_FIELDS['inputs']: document,
        CONTRACT_FIELDS['fingerprint']:
            fingerprint_module.fingerprint_of(document),
        CONTRACT_FIELDS['uncertainty_basis']: uncertainty_basis,
        CONTRACT_FIELDS['resolution_m']: resolution_m,
        CONTRACT_FIELDS['levels']: list(levels) if levels else [level],
        CONTRACT_FIELDS['tile']: {'level': level, 'row': row, 'col': col},
    }
    properties.update(dated(start_datetime, end_datetime,
                            what=f'tile Item {identifier!r}'))
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
        'id': identifier,
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
    return {
        'type': 'Collection',
        'stac_version': STAC_VERSION,
        'id': collection_id(quantity, state, origin),
        'description': description,
        'license': license_id,
        'extent': {
            'spatial': {'bbox': spatial},
            'temporal': {'interval': [collection_interval(items)]},
        },
        'links': [],
        'properties': {
            CONTRACT_FIELDS['quantity']: Quantity(quantity).value,
            CONTRACT_FIELDS['state']: State(state).value,
            CONTRACT_FIELDS['origin']: Origin(origin).value,
            CONTRACT_FIELDS['frame']: collection_frame(items),
        },
        'summaries': {
            CONTRACT_FIELDS['levels']: sorted({
                lvl for item in items
                for lvl in item['properties'].get(
                    CONTRACT_FIELDS['levels'], [])}),
        },
    }


def collection_interval(items: Sequence[Mapping[str, Any]]) -> List[Any]:
    """
    Return the Collection's overall temporal extent: the union of its Items'.

    STAC reads ``extent.temporal.interval[0]`` as the Collection's overall
    extent (later entries are optional sub-intervals). Listing every Item's
    interval in id order therefore declared the FIRST tile's window as the
    whole Collection's, with one duplicate per tile after it. The union is the
    one honest answer; ``[None, None]`` (open at both ends) is STAC's spelling
    of "unknown" for a Collection with no Item yet.

    An Item with a single ``datetime`` contributes that instant.

    :raises ItemSchemaError: when an Item carries neither shape -- an undated
        Item is refused by every writer, so reaching one here is a defect.
    """
    pairs = []
    for item in items:
        properties = item.get('properties') or {}
        instant = properties.get('datetime')
        if instant is not None:
            pairs.append((instant, instant))
        else:
            pairs.append((properties.get('start_datetime'),
                          properties.get('end_datetime')))
    if not pairs:
        return [None, None]
    try:
        start, end = source_time.union_intervals(pairs, what='a Collection')
    except source_time.TimeIntervalError as exc:
        raise ItemSchemaError(str(exc)) from exc
    return [start, end]


def collection_frame(items: Sequence[Mapping[str, Any]]) -> Dict[str, Any]:
    """
    Return the frame a Collection declares: the one its Items all declare.

    A Collection that named the store frame over Items that declare an
    untransformed legacy frame repeats the false claim the Item-level
    ``frame=`` override exists to avoid. With no Item yet, the store frame is
    what the cell will hold.

    :raises ItemSchemaError: when the Items disagree -- one tile tree in two
        frames is not a Collection a consumer can read by one declaration.
    """
    frames = []
    for item in items:
        frame = (item.get('properties') or {}).get(CONTRACT_FIELDS['frame'])
        if frame is not None and frame not in frames:
            frames.append(frame)
    if not frames:
        return store_frame()
    if len(frames) > 1:
        raise ItemSchemaError(
            f'the Items of one Collection declare {len(frames)} different '
            f'frames ({[f.get("epsg") for f in frames]}); one tile tree must '
            'be in one frame')
    return dict(frames[0])


def validate_contract(item: Mapping[str, Any]) -> None:
    """
    Fail on an Item missing a field Part 2 line 2 promises a consumer.

    ``resolution_m`` may be present and null -- a producer that does not know a
    tile's ground resolution says so rather than inventing one -- but the key
    itself must be there, so the absence is a statement rather than a gap.

    The time range is checked too, in the shape STAC allows: an Item either
    carries a ``datetime``, or a null one with **both** ends of a range. An
    Item with neither is not under-specified, it is invalid.

    :raises ItemSchemaError: naming every missing field at once; a writer
        fixing them one at a time learns the schema one round trip at a time.
    """
    properties = item.get('properties') or {}
    if properties.get('datetime') is None:
        dated(properties.get('start_datetime'),
              properties.get('end_datetime'),
              what=f'Item {item.get("id")!r}')
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
    :param start_datetime: start of the interval the source was recorded
        over, from :func:`marine_world_store.source_time.source_interval`.
        **Required**, with ``end_datetime``.
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
        'license': license_id,
        CONTRACT_FIELDS['source']: {
            'id': source_id,
            'kind': kind,
            'name': name,
            'role': role,
            'file_keys': list(file_keys or []),
        },
    }
    properties.update(dated(start_datetime, end_datetime,
                            what=f'source Item {source_id!r}'))
    for key, value in (('platform', platform), ('recorder', recorder)):
        if value:
            properties[f'{PREFIX}:{key}'] = value
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
