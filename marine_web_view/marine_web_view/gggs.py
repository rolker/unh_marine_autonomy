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

# `gggs::levels` is a fixed std::array<LevelSpecs, 21> (`level_spec.h:109`) and
# `Level`'s constructor throws for anything past its end (`level.h:52`), so 20
# is the deepest real level. Named here rather than spelled inline so the one
# place that has to track the C++ array is obvious.
MAX_LEVEL = 20


def grid_angular_span(level):
    """Return the latitude span of one grid at this level, in degrees."""
    return LEVEL_0_GRID_ANGULAR_SPAN / float(1 << level)


def row_count(level):
    """Return the total number of grid rows at this level."""
    return LEVEL_0_ROW_COUNT * (1 << level)


def latitude_scale_factor(level, row):
    """Return the longitude scale factor (1, 3 or 9) for a grid row.

    Mirrors `LevelSpecs::latitudeScaleFactor` (`level_spec.h:66`). The C++
    boundaries are `uint32_t` members assigned from a double division
    (`level_spec.h:58-61`), so they truncate -- they are integers, not the
    fractional edges an earlier version of this docstring claimed. They happen
    to be exact at every level (16/8, 24/8, 168/8 and 176/8 are all whole
    numbers, scaled by 2**level), so truncation changes nothing today; `int()`
    is applied anyway so this stays a faithful port if level 0 geometry ever
    moves.
    """
    span = grid_angular_span(level)
    row_minus_80 = int((-80.0 - LATITUDE_ORIGIN) / span)
    row_minus_72 = int((-72.0 - LATITUDE_ORIGIN) / span)
    row_plus_72 = int((72.0 - LATITUDE_ORIGIN) / span)
    row_plus_80 = int((80.0 - LATITUDE_ORIGIN) / span)
    if row_minus_72 <= row < row_plus_72:
        return 1
    if row_minus_80 <= row < row_plus_80:
        return 3
    return 9


def scale_factor_for_latitude(latitude):
    """Return the longitude scale factor (1, 3 or 9) for a latitude.

    Mirrors the free `gggs::latitudeScaleFactor(double)` (`core.h:96`), which
    is what `Level::gridIndex` uses to size a column -- NOT the row-indexed
    overload used for a grid's own extent. The two agree everywhere except
    within one grid of a band edge, where they disagree by a whole grid.
    """
    magnitude = abs(latitude)
    if magnitude < 72.0:
        return 1
    if magnitude < 80.0:
        return 3
    return 9


def normalize_longitude(longitude):
    """Wrap a longitude into the half-open range [-180, 180).

    Mirrors `gggs::normalizeLongitude` (`core.h:113`). Without it a longitude
    of 185 deg indexes a column past the end of its row rather than -175.
    """
    wrapped = math.fmod(longitude + 180.0, 360.0)
    if wrapped < 0.0:
        wrapped += 360.0
    return wrapped - 180.0


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


LATITUDE_EPSILON = 1e-6


def grid_index_for(level, latitude, longitude):
    """Return (row, column) of the grid containing a position.

    Mirrors `Level::gridIndex` (`level.h:89`) including the two guards it
    applies before the arithmetic and that this port originally omitted:
    longitude is wrapped into [-180, 180), and latitude is clamped for
    floating-point overshoot but raises for anything clearly out of range.
    The column's scale factor comes from the LATITUDE, matching the C++;
    using the row-indexed overload instead would place positions within one
    grid of +/-72 or +/-80 deg in the wrong column.
    """
    if not (-90.0 - LATITUDE_EPSILON <= latitude <= 90.0 + LATITUDE_EPSILON):
        raise ValueError(
            'GGGS latitude {} is out of range [-90, 90]'.format(latitude))
    latitude = _clamp(latitude, -90.0, 90.0)
    longitude = normalize_longitude(longitude)

    span = grid_angular_span(level)
    row = int((latitude - LATITUDE_ORIGIN) / span)
    lon_span = span * scale_factor_for_latitude(latitude)
    column = int((longitude - LONGITUDE_ORIGIN) / lon_span)
    return row, column


def _clamp(value, low, high):
    """Clamp value into [low, high]."""
    return max(low, min(high, value))


# ---------------------------------------------------------------------------
# Web Mercator (slippy) tiles -- the projection the web view renders in.
# ---------------------------------------------------------------------------

def tile_coordinates(zoom, longitude, latitude):
    """Return the FRACTIONAL (x, y) slippy tile coordinates of a position.

    Kept separate from `lonlat_to_tile` because a covering set has to reason
    about tile-space intervals, not about which tile a boundary value floors
    into: `tile_bounds` and this function are inverses only to within float
    round-trip error, so re-indexing a returned edge can land one tile over.
    """
    n = 1 << zoom
    x = (longitude + 180.0) / 360.0 * n
    lat_rad = math.radians(latitude)
    y = (1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n
    return x, y


def lonlat_to_tile(zoom, longitude, latitude):
    """Return the (x, y) slippy tile containing a position."""
    n = 1 << zoom
    x, y = tile_coordinates(zoom, longitude, latitude)
    return _clamp(int(x), 0, n - 1), _clamp(int(y), 0, n - 1)


def tile_bounds(zoom, x, y):
    """Return (south, west, north, east) degrees for a slippy tile."""
    n = 1 << zoom
    west = x / n * 360.0 - 180.0
    east = (x + 1) / n * 360.0 - 180.0
    north = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / n))))
    south = math.degrees(
        math.atan(math.sinh(math.pi * (1.0 - 2.0 * (y + 1) / n))))
    return south, west, north, east


# Tolerance, in tile units, for the half-open interval arithmetic below.
# `tile_bounds` and `tile_coordinates` are inverses only to within double
# round-trip error (~1e-12 tiles here); 1e-6 of a tile is about a millimetre
# of ground at zoom 15, far below a cell, and far above that error.
TILE_EPSILON = 1e-6


def tiles_covering(zoom, south, west, north, east):
    """Yield every (x, y) slippy tile overlapping a geographic box.

    The box is treated as HALF-OPEN -- [south, north) x [west, east) -- which
    is what a GGGS grid extent is: a cell on the north or east edge belongs
    to the next grid over. Working from the raw edge values instead adds a
    spurious row and column of tiles per grid: tiles holding none of this
    grid's cells that still get marked dirty by it, then rendered and
    uploaded on every pass.

    The interval arithmetic is done in tile space rather than by indexing the
    edges, because a slippy tile is half-open the other way round in latitude
    (`lonlat_to_tile` floors a decreasing function, so a boundary latitude
    resolves to the tile below) and because the projection round-trip is not
    exact.
    """
    if north <= south or east <= west:
        return
    count = 1 << zoom
    x_west, y_north = tile_coordinates(zoom, west, north)
    x_east, y_south = tile_coordinates(zoom, east, south)

    x0 = _clamp(int(math.floor(x_west + TILE_EPSILON)), 0, count - 1)
    x1 = _clamp(int(math.ceil(x_east - TILE_EPSILON)) - 1, 0, count - 1)
    y0 = _clamp(int(math.floor(y_north + TILE_EPSILON)), 0, count - 1)
    y1 = _clamp(int(math.ceil(y_south - TILE_EPSILON)) - 1, 0, count - 1)

    for x in range(min(x0, x1), max(x0, x1) + 1):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            yield x, y
