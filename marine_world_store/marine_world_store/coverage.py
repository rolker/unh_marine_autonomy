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
Read ``marine_tiled_raster_store``'s coverage manifest from Python.

``coverage.json`` (schema ``coverage-manifest/1``) is where the C++ writers
already record each tile's ``geometric_error_m`` -- uma-ADR-0013 D1/D2/D3's
producer obligation. The rev-3 Items carry that same number, so this module
*reads the existing convention* rather than defining a parallel one; the
authority is ``marine_tiled_raster_store/include/.../coverage_manifest.hpp``.

Tolerant in the same way the C++ reader is (D8: the manifest is derived and
advisory): a missing, malformed or wrong-schema document yields ``None``, and
:func:`scan_coverage` recovers the tile set from filenames alone. What a caller
must **not** do is treat a missing error as a small one -- an absent
``geometric_error_m`` is ``None``, never ``0``.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
import math
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from marine_world_store import layout

PathLike = Union[str, Path]

#: The only document schema this reader accepts, matching ``kSchema``.
SCHEMA = 'coverage-manifest/1'

#: ``(level, row, col)`` -- the key both sides use for a tile.
TileKey = Tuple[int, int, int]

#: GGGS levels are 0..20 (``gggs::levels.size() == 21``); the C++ reader
#: refuses a document naming any other.
MAX_LEVEL = 20

#: Grid indices are ``uint32_t`` on the C++ side.
MAX_INDEX = 0xFFFFFFFF

#: Total tiles one document may expand to -- the C++ reader's
#: ``kMaxDecodedGrids``. A real layer is thousands of tiles; this only ever
#: trips on corruption, where a few hundred bytes of JSON declaring a
#: full-width run would otherwise expand into tens of millions of entries.
MAX_DECODED_TILES = 5_000_000


@dataclass
class CoverageManifest:
    """Which tiles a layer holds, and each tile's geometric error."""

    kind: str = ''
    errors: Dict[TileKey, Optional[float]] = field(default_factory=dict)

    def contains(self, key: TileKey) -> bool:
        """Whether the layer holds a tile at ``key``."""
        return key in self.errors

    def geometric_error(self, key: TileKey) -> Optional[float]:
        """
        Return the recorded error for ``key``, or ``None``.

        The two ``None`` cases are deliberately not distinguished here: a
        consumer falls back to level-as-resolution for both.
        """
        return self.errors.get(key)

    def levels(self) -> List[int]:
        """Levels holding at least one tile, coarsest first."""
        return sorted({key[0] for key in self.errors})

    def tiles_at(self, level: int) -> List[TileKey]:
        """Tiles held at ``level``, in GGGS order (row then column)."""
        return sorted(key for key in self.errors if key[0] == level)

    def __len__(self) -> int:
        """Count the tiles held, across all levels."""
        return len(self.errors)


def load_coverage_manifest(path: PathLike) -> Optional[CoverageManifest]:
    """
    Read a ``coverage-manifest/1`` document, or ``None``.

    Never raises on a bad document: the manifest is advisory, and a consumer
    that cannot read it is no worse off than before the manifest existed.

    Refuses (``None``) what the C++ reader refuses, so the two agree on which
    documents are manifests: a level outside GGGS, a row or column that is not
    a non-negative integer in ``uint32`` range, a backwards run, and a
    ``geometric_error_m`` that is a number but not a finite, non-negative
    length (NaN, inf or negative would read as "infinitely precise" to an LOD
    consumer). A document expanding past :data:`MAX_DECODED_TILES` is refused
    too; the C++ reader truncates it instead -- both end the read, and a
    refused manifest falls back to :func:`scan_coverage` here.
    """
    path = Path(path)
    try:
        document = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    if not isinstance(document, dict) or document.get('schema') != SCHEMA:
        return None
    levels = document.get('levels')
    if not isinstance(levels, list):
        return None
    manifest = CoverageManifest(kind=str(document.get('kind', '')))
    decoded = 0
    for level_entry in levels:
        if not isinstance(level_entry, dict):
            return None
        level = level_entry.get('level')
        runs = level_entry.get('runs')
        if not _index(level, MAX_LEVEL) or not isinstance(runs, list):
            return None
        for run in runs:
            if not isinstance(run, dict):
                return None
            row, col_min, col_max = (
                run.get('row'), run.get('col_min'), run.get('col_max'))
            if not all(_index(v, MAX_INDEX) for v in (row, col_min, col_max)):
                return None
            if col_max < col_min:
                return None
            error = _geometric_error(run.get('geometric_error_m'))
            if error is _INVALID:
                return None
            decoded += col_max - col_min + 1
            if decoded > MAX_DECODED_TILES:
                return None
            for col in range(col_min, col_max + 1):
                manifest.errors[(level, row, col)] = error
    return manifest


#: Marks a ``geometric_error_m`` that makes the whole document unreadable.
_INVALID = object()


def _index(value, maximum: int) -> bool:
    """A JSON unsigned integer no larger than ``maximum`` (never a bool)."""
    return (isinstance(value, int) and not isinstance(value, bool)
            and 0 <= value <= maximum)


def _geometric_error(value):
    """
    One run's error: a length, ``None`` when unrecorded, or :data:`_INVALID`.

    Mirrors the C++ reader: a field that is not a number is treated as absent
    (``None``, never 0), and a number that is not a finite, non-negative length
    invalidates the document rather than being read.
    """
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    value = float(value)
    if not math.isfinite(value) or value < 0.0:
        return _INVALID
    return value


def load_layer_coverage(layer_dir: PathLike) -> Optional[CoverageManifest]:
    """:func:`load_coverage_manifest` for a layer's own ``coverage.json``."""
    return load_coverage_manifest(layout.coverage_manifest_path(layer_dir))


def scan_coverage(layer_dir: PathLike) -> CoverageManifest:
    """
    Recover the tile set from filenames -- the no-manifest fallback.

    Carries no geometric error: a scan can only see filenames (the same
    statement ``scanCoverage()`` makes on the C++ side).
    """
    manifest = CoverageManifest(kind='scanned')
    for path in layout.tiles_in_dir(layer_dir):
        parsed = layout.parse_tile_filename(path.name)
        if parsed is not None:
            manifest.errors[parsed] = None
    return manifest


def coverage_for_layer(layer_dir: PathLike) -> CoverageManifest:
    """Return the layer's manifest, or a scan of it when there is none."""
    manifest = load_layer_coverage(layer_dir)
    return scan_coverage(layer_dir) if manifest is None else manifest
