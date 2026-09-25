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
Read a tile's geographic footprint from the raster itself.

Part 2 line 1 promises a consumer can find a product by extent, so an Item
without a footprint is a weaker promise than the contract makes. The footprint
is taken from the file's own georeferencing rather than recomputed from GGGS
arithmetic: a second implementation of the grid maths in Python is exactly the
per-consumer code this package exists to avoid, and the authority is the tile.

GDAL does the reading (``python3-gdal``, a declared dependency). The import is
local to the call so the rest of the library stays importable without it.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, Optional, Tuple, Union

PathLike = Union[str, Path]


class FootprintError(RuntimeError):
    """A raster whose footprint could not be read."""


def tile_footprint(path: PathLike) -> Tuple[Dict[str, Any], list]:
    """
    Read the ``(geometry, bbox)`` of ``path``, in its own georeferencing.

    :raises FootprintError: when GDAL is unavailable, the file will not open,
        or it carries no geotransform. Every one of those means the Item's
        extent would be a guess, and a guessed extent is worse than a loud
        failure -- a consumer searching by extent would silently miss the tile.
    """
    path = Path(path)
    try:
        from osgeo import gdal
    except ImportError as exc:
        raise FootprintError(
            'python3-gdal is required to read a tile footprint; it is a '
            'declared dependency of this package (rosdep key python3-gdal). '
            f'({exc})') from exc

    gdal.UseExceptions()
    try:
        dataset = gdal.Open(str(path))
    except RuntimeError as exc:
        raise FootprintError(f'{path}: {exc}') from exc
    if dataset is None:
        raise FootprintError(f'{path}: GDAL could not open it')
    transform = dataset.GetGeoTransform(can_return_null=True)
    if transform is None:
        raise FootprintError(f'{path}: no geotransform; extent unknowable')
    width = dataset.RasterXSize
    height = dataset.RasterYSize
    corners = [
        _apply(transform, 0, 0),
        _apply(transform, width, 0),
        _apply(transform, width, height),
        _apply(transform, 0, height),
    ]
    xs = [x for x, _ in corners]
    ys = [y for _, y in corners]
    bbox = [min(xs), min(ys), max(xs), max(ys)]
    ring = [list(corners[i]) for i in (0, 1, 2, 3)] + [list(corners[0])]
    return {'type': 'Polygon', 'coordinates': [ring]}, bbox


def cell_size_m(path: PathLike, latitude: Optional[float] = None
                ) -> Optional[float]:
    """
    Approximate north-south cell size of ``path`` in metres.

    North-south only, and approximate: a GGGS cell's east-west extent varies
    with latitude by design, so one number cannot be both. Reported as the
    Item's ``resolution_m`` because that is the figure a consumer compares
    against a required resolution; ``None`` when it cannot be read, which the
    Item then records as a null rather than as a guess.
    """
    del latitude  # reserved: an east-west figure would need the row's latitude
    try:
        from osgeo import gdal
    except ImportError:
        return None
    gdal.UseExceptions()
    try:
        dataset = gdal.Open(str(path))
    except RuntimeError:
        return None
    if dataset is None:
        return None
    transform = dataset.GetGeoTransform(can_return_null=True)
    if transform is None:
        return None
    # Degrees of latitude are very nearly constant in metres; this is the
    # figure the existing store's own docs quote for a cell.
    metres_per_degree = 111320.0
    return abs(transform[5]) * metres_per_degree


def _apply(transform, pixel: int, line: int) -> Tuple[float, float]:
    x = transform[0] + pixel * transform[1] + line * transform[2]
    y = transform[3] + pixel * transform[4] + line * transform[5]
    return x, y
