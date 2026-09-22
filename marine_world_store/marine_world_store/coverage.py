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
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from marine_world_store import layout

PathLike = Union[str, Path]

#: The only document schema this reader accepts, matching ``kSchema``.
SCHEMA = 'coverage-manifest/1'

#: ``(level, row, col)`` -- the key both sides use for a tile.
TileKey = Tuple[int, int, int]


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
    for level_entry in levels:
        if not isinstance(level_entry, dict):
            return None
        level = level_entry.get('level')
        runs = level_entry.get('runs')
        if not isinstance(level, int) or not isinstance(runs, list):
            return None
        for run in runs:
            if not isinstance(run, dict):
                return None
            try:
                row = int(run['row'])
                col_min = int(run['col_min'])
                col_max = int(run['col_max'])
            except (KeyError, TypeError, ValueError):
                return None
            if col_max < col_min:
                return None
            error = run.get('geometric_error_m')
            error = None if error is None else float(error)
            for col in range(col_min, col_max + 1):
                manifest.errors[(level, row, col)] = error
    return manifest


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
