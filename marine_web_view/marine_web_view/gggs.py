#!/usr/bin/env python3

# Copyright 2026 Roland Arsenault
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
#    * Neither the name of the Roland Arsenault nor the names of its
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

"""GGGS grid geometry, in pure Python.

Port of the bounds math that is authoritative in C++ at
`marine_autonomy/include/marine_autonomy/gggs/{core,grid_index,level_spec}.h`.
Kept ROS-free so it can be unit-tested without a node.

CAUTION -- this is a cross-language duplicate of a correctness-critical spec.
If the C++ headers change (level-0 geometry, the polar longitude scale factors,
or the extent formulas), this module drifts silently: nothing links them. The
tests pin known index -> extent values so a divergence fails loudly here rather
than mis-georeferencing a coverage tile.

Geometry, from `core.h`:
  * level 0 grids span 8 deg; each level halves that span
  * rows are numbered south to north from -96 deg latitude (24 rows at level 0)
  * columns are numbered west to east from -180 deg longitude (45 at level 0)
  * grids are 960 x 960 cells

Longitude spans widen toward the poles so grids stay roughly square: the scale
factor is 1 equatorward of 72 deg, 3 between 72 and 80, and 9 poleward of 80.
"""

import math

LEVEL_0_GRID_ANGULAR_SPAN = 8.0
LEVEL_0_ROW_COUNT = 24
LEVEL_0_COLUMN_COUNT = 45
CELL_ROWS_PER_GRID = 960
CELL_COLUMNS_PER_GRID = 960

# Rows are indexed from -96 deg, not -90: the grid covers 192 deg of latitude
# so that whole grids tile the poles.
LATITUDE_ORIGIN = -96.0
LONGITUDE_ORIGIN = -180.0


def grid_angular_span(level):
    """Return the latitude span of one grid at this level, in degrees."""
    return LEVEL_0_GRID_ANGULAR_SPAN / float(1 << level)


def row_count(level):
    """Return the total number of grid rows at this level."""
    return LEVEL_0_ROW_COUNT * (1 << level)


def latitude_scale_factor(level, row):
    """Return the longitude scale factor (1, 3 or 9) for a grid row.

    Mirrors `LevelSpecs::latitudeScaleFactor`. The comparisons are against
    fractional row boundaries in the C++ too, so they are kept as floats
    rather than rounded -- rounding would move the band edges by up to half a
    grid.
    """
    span = grid_angular_span(level)
    row_minus_80 = (-80.0 - LATITUDE_ORIGIN) / span
    row_minus_72 = (-72.0 - LATITUDE_ORIGIN) / span
    row_plus_72 = (72.0 - LATITUDE_ORIGIN) / span
    row_plus_80 = (80.0 - LATITUDE_ORIGIN) / span
    if row_minus_72 <= row < row_plus_72:
        return 1
    if row_minus_80 <= row < row_plus_80:
        return 3
    return 9


def grid_longitudinal_span(level, row):
    """Return the longitude span of one grid at this level and row."""
    return grid_angular_span(level) * latitude_scale_factor(level, row)


def column_count(level, row):
    """Return the number of grid columns in this row."""
    return (LEVEL_0_COLUMN_COUNT * (1 << level)
            // latitude_scale_factor(level, row))


def grid_bounds(level, row, column):
    """Return (south, west, north, east) degrees for a grid index.

    Mirrors `GridIndex::{south,north}Latitude` and
    `GridIndex::{west,east}Longitude`, including the latitude clamp: rows run
    to +/-96 deg but geographic latitude stops at +/-90.
    """
    span = grid_angular_span(level)
    south = _clamp(LATITUDE_ORIGIN + row * span, -90.0, 90.0)
    north = _clamp(LATITUDE_ORIGIN + (row + 1) * span, -90.0, 90.0)
    lon_span = grid_longitudinal_span(level, row)
    west = LONGITUDE_ORIGIN + column * lon_span
    east = LONGITUDE_ORIGIN + (column + 1) * lon_span
    return south, west, north, east


def grid_index_for(level, latitude, longitude):
    """Return (row, column) of the grid containing a position."""
    span = grid_angular_span(level)
    row = int((latitude - LATITUDE_ORIGIN) / span)
    lon_span = grid_longitudinal_span(level, row)
    column = int((longitude - LONGITUDE_ORIGIN) / lon_span)
    return row, column


def _clamp(value, low, high):
    """Clamp value into [low, high]."""
    return max(low, min(high, value))


# ---------------------------------------------------------------------------
# Web Mercator (slippy) tiles -- the projection the web view renders in.
# ---------------------------------------------------------------------------

def lonlat_to_tile(zoom, longitude, latitude):
    """Return the (x, y) slippy tile containing a position."""
    n = 1 << zoom
    x = int((longitude + 180.0) / 360.0 * n)
    lat_rad = math.radians(latitude)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return _clamp(x, 0, n - 1), _clamp(y, 0, n - 1)


def tile_bounds(zoom, x, y):
    """Return (south, west, north, east) degrees for a slippy tile."""
    n = 1 << zoom
    west = x / n * 360.0 - 180.0
    east = (x + 1) / n * 360.0 - 180.0
    north = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / n))))
    south = math.degrees(
        math.atan(math.sinh(math.pi * (1.0 - 2.0 * (y + 1) / n))))
    return south, west, north, east


def tiles_covering(zoom, south, west, north, east):
    """Yield every (x, y) slippy tile overlapping a geographic box."""
    x0, y0 = lonlat_to_tile(zoom, west, north)
    x1, y1 = lonlat_to_tile(zoom, east, south)
    for x in range(min(x0, x1), max(x0, x1) + 1):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            yield x, y
