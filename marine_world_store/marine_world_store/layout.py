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
The rev-3 on-disk layout: ``<root>/<quantity>/<state>/<origin>/``.

Design draft sections 2 and 5. Two things this module exists to prevent:

* a typo'd directory name silently becoming a new folder -- ``state`` and
  ``origin`` are enums, so ``"reviwed"`` is an error at the call site;
* a second copy of the path arithmetic in each consumer -- CAMP, the survey
  explorer and the costmap all need the same answers.

The layout is built **alongside** the existing store: nothing here reads or
writes ``draft/``, ``processed/``, ``reference/`` or ``chart/``, which are
``marine_bathymetry_store``'s tree and are unaffected by this package.
"""

from __future__ import annotations

from enum import Enum
from pathlib import Path
from typing import Iterable, Union

PathLike = Union[str, Path]


class State(str, Enum):
    """How far *our* processing has gone (design section 5, spine 3)."""

    DRAFT = 'draft'
    REVIEWED = 'reviewed'
    PUBLISHED = 'published'


class Origin(str, Enum):
    """Our own surveys, or material received from outside."""

    SURVEYED = 'surveyed'
    IMPORTED = 'imported'


class Quantity(str, Enum):
    """A store whose products are addressed by ``state`` and ``origin``."""

    DEPTHS = 'depths'
    BACKSCATTER = 'backscatter'
    SIDESCAN = 'sidescan'
    WATER = 'water'
    FEATURES = 'features'
    DERIVED = 'derived'


#: Categories that are not quantity stores and carry no state/origin axes.
SOURCES_DIR = 'sources'
REVISIONS_DIR = 'revisions'

#: Categories that exist only for our own surveys (design section 5: "Traject-
#: ories and observations have only ``surveyed/``").
TRAJECTORIES_DIR = 'trajectories'
OBSERVATIONS_DIR = 'observations'

#: Filename of a layer's derived coverage manifest, as written by
#: ``marine_tiled_raster_store`` (``coverageManifestFilename()``). Named here so
#: the Python side reads the C++ side's convention rather than inventing one.
COVERAGE_MANIFEST_FILENAME = 'coverage.json'

#: Sidecar directory of folded levels inside a layer directory, as
#: ``marine_bathymetry_store``'s pyramid builder writes it.
OVERVIEWS_DIR = 'overviews'

#: Item/Collection filenames within a directory.
COLLECTION_FILENAME = 'collection.json'


class LayoutError(ValueError):
    """A layout request that the rev-3 model does not have a place for."""


def _coerce(value, enum_type):
    """Accept an enum member or its exact string spelling, nothing else."""
    if isinstance(value, enum_type):
        return value
    try:
        return enum_type(value)
    except ValueError as exc:
        allowed = ', '.join(sorted(m.value for m in enum_type))
        raise LayoutError(
            f'{value!r} is not a {enum_type.__name__}; expected one of '
            f'{allowed}') from exc


def quantity_dir(
    root: PathLike,
    quantity: Union[Quantity, str],
    state: Union[State, str],
    origin: Union[Origin, str],
) -> Path:
    """
    ``<root>/<quantity>/<state>/<origin>/``.

    Origin is a directory, not only an Item field: consumers order by origin
    and the per-cell provenance carries no source, so two origins cannot share
    one tile tree (design section 5).
    """
    quantity = _coerce(quantity, Quantity)
    state = _coerce(state, State)
    origin = _coerce(origin, Origin)
    return Path(root) / quantity.value / state.value / origin.value


def sources_dir(root: PathLike) -> Path:
    """``<root>/sources/`` -- material as received, content-identified."""
    return Path(root) / SOURCES_DIR


def revisions_dir(root: PathLike) -> Path:
    """``<root>/revisions/`` -- append-only reviewed records."""
    return Path(root) / REVISIONS_DIR


def trajectories_dir(
        root: PathLike, origin: Union[Origin, str] = Origin.SURVEYED) -> Path:
    """
    ``<root>/trajectories/surveyed/``.

    :raises LayoutError: for any origin but ``surveyed`` -- a trajectory is
        built from our own bags by definition; an imported one would be a
        different product with a different provenance story.
    """
    return _surveyed_only(root, TRAJECTORIES_DIR, origin)


def observations_dir(
        root: PathLike, origin: Union[Origin, str] = Origin.SURVEYED) -> Path:
    """``<root>/observations/surveyed/`` (see :func:`trajectories_dir`)."""
    return _surveyed_only(root, OBSERVATIONS_DIR, origin)


def _surveyed_only(root: PathLike, name: str,
                   origin: Union[Origin, str]) -> Path:
    origin = _coerce(origin, Origin)
    if origin is not Origin.SURVEYED:
        raise LayoutError(
            f'{name}/ exists only for surveyed material (design section 5); '
            f'got origin={origin.value}')
    return Path(root) / name / origin.value


def overviews_dir(layer_dir: PathLike) -> Path:
    """Return the folded-level sidecar inside a quantity layer directory."""
    return Path(layer_dir) / OVERVIEWS_DIR


def coverage_manifest_path(layer_dir: PathLike) -> Path:
    """``<layer_dir>/coverage.json``."""
    return Path(layer_dir) / COVERAGE_MANIFEST_FILENAME


def collection_path(directory: PathLike) -> Path:
    """``<directory>/collection.json``."""
    return Path(directory) / COLLECTION_FILENAME


def tile_filename(level: int, row: int, col: int) -> str:
    """
    ``<level>_<row>_<col>.tif`` -- the tile naming both stores use.

    ``marine_tiled_raster_store::tileFilename`` is the authority; this is the
    same spelling so a Python reader and the C++ writer agree.
    """
    for name, value in (('level', level), ('row', row), ('col', col)):
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise LayoutError(f'{name} must be a non-negative int, '
                              f'got {value!r}')
    return f'{level}_{row}_{col}.tif'


def parse_tile_filename(name: str):
    """
    Inverse of :func:`tile_filename`; ``None`` when ``name`` is not a tile.

    Tolerant on purpose: a layer directory legitimately holds non-tile files
    (``coverage.json``, Items), and a reader that threw on them would make
    every caller write the same try/except.
    """
    stem, dot, suffix = str(name).rpartition('.')
    if not dot or suffix != 'tif':
        return None
    parts = stem.split('_')
    if len(parts) != 3:
        return None
    try:
        level, row, col = (int(p) for p in parts)
    except ValueError:
        return None
    if min(level, row, col) < 0:
        return None
    return level, row, col


def tiles_in_dir(directory: PathLike) -> Iterable[Path]:
    """Every tile file in ``directory``, in GGGS order (level, row, column)."""
    directory = Path(directory)
    if not directory.is_dir():
        return []
    found = []
    for path in directory.iterdir():
        parsed = parse_tile_filename(path.name)
        if parsed is not None:
            found.append((parsed, path))
    return [path for _, path in sorted(found, key=lambda item: item[0])]
