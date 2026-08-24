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

"""Pin the north/south orientation of tile sampling.

GGGS cell rows are numbered FROM THE SOUTH (gggs/cell_index.h: "positive
integer row starting the bottom of the grid") while image rows run north to
south. Getting that backwards mirrors every tile vertically inside its own
box -- about 870 m at level 10 -- which does not look like a flip. It looks
like coverage that drifts off the vessel's track, which is how it was found:
on screen, over real water, after the code had been reviewed and tested.

A flip is invisible to any test that uses symmetric data, so this one builds a
tile whose value encodes its own latitude and checks the gradient survives
sampling with the right sign.
"""

from marine_web_view import gggs
from marine_web_view.coverage_renderer import CoverageRenderer

import numpy


class _Sampler:
    """Minimal stand-in exposing the sampler over a hand-built cache."""

    def __init__(self, tiles, zoom):
        self._tiles = tiles
        self.zoom = zoom
        import threading
        self._lock = threading.Lock()

    _candidates = CoverageRenderer._candidates
    _sample_tile = CoverageRenderer._sample_tile


def test_sampling_preserves_north_south_orientation():
    """A north-shallow, south-deep tile must sample that way round.

    The tile is filled so its value increases with GGGS cell row, i.e. with
    latitude. After sampling, the top image rows (north) must therefore hold
    the LARGER values. An inverted cell-row mapping reverses that.
    """
    level, row, col = 10, 17790, 14000
    cells = 960
    tile = numpy.zeros((cells, cells), dtype=numpy.float32)
    for cell_row in range(cells):
        tile[cell_row, :] = float(cell_row)     # increases northward

    south, west, north, east = gggs.grid_bounds(level, row, col)
    centre_lat = (south + north) / 2.0
    centre_lon = (west + east) / 2.0
    zoom = 17          # small enough that one slippy tile sits inside the grid
    x, y = gggs.lonlat_to_tile(zoom, centre_lon, centre_lat)

    sampler = _Sampler({(level, row, col): tile}, zoom)
    out = sampler._sample_tile(x, y)

    covered = numpy.isfinite(out)
    assert covered.any(), 'sampled nothing -- the tile and slippy tile missed'

    rows_with_data = numpy.flatnonzero(covered.any(axis=1))
    top = out[rows_with_data[0]][covered[rows_with_data[0]]].mean()
    bottom = out[rows_with_data[-1]][covered[rows_with_data[-1]]].mean()
    assert top > bottom, (
        'north edge sampled {:.0f} and south edge {:.0f}: cell rows are '
        'numbered from the south, so the north edge must hold the larger '
        'value -- the tile is being sampled upside down'.format(top, bottom))


def test_sampling_is_monotonic_down_the_image():
    """Values must fall steadily from the north edge to the south edge."""
    level, row, col = 10, 17790, 14000
    cells = 960
    tile = numpy.zeros((cells, cells), dtype=numpy.float32)
    for cell_row in range(cells):
        tile[cell_row, :] = float(cell_row)

    south, west, north, east = gggs.grid_bounds(level, row, col)
    zoom = 17
    x, y = gggs.lonlat_to_tile(zoom, (west + east) / 2.0, (south + north) / 2.0)
    out = _Sampler({(level, row, col): tile}, zoom)._sample_tile(x, y)

    covered = numpy.isfinite(out)
    means = [out[r][covered[r]].mean()
             for r in numpy.flatnonzero(covered.any(axis=1))]
    assert len(means) > 8, 'too few covered rows to judge a gradient'
    decreasing = sum(1 for a, b in zip(means, means[1:]) if a >= b)
    assert decreasing >= len(means) - 2, (
        'values do not fall consistently from north to south')


def _uniform_tile(level, row, col, value, cells=960):
    """Return a grid array filled with one value."""
    return numpy.full((cells, cells), value, dtype=numpy.float32)


def test_the_finer_level_wins_where_two_cover_the_same_ground():
    """Overlapping levels must resolve by resolution, not dict order."""
    coarse = (9, 8895, 6994)
    fine = (10, 17790, 13988)
    south, west, north, east = gggs.grid_bounds(*fine)
    zoom = 17
    x, y = gggs.lonlat_to_tile(zoom, (west + east) / 2.0,
                               (south + north) / 2.0)

    tiles = {coarse: _uniform_tile(*coarse, value=-10.0),
             fine: _uniform_tile(*fine, value=-20.0)}
    out = _Sampler(tiles, zoom)._sample_tile(x, y)
    covered = numpy.isfinite(out)
    assert covered.any()
    assert numpy.allclose(out[covered], -20.0), (
        'the coarser level painted over the finer one')

    # Insertion order must not change the answer.
    reversed_tiles = {fine: tiles[fine], coarse: tiles[coarse]}
    out2 = _Sampler(reversed_tiles, zoom)._sample_tile(x, y)
    assert numpy.allclose(out2[numpy.isfinite(out2)], -20.0)


def test_a_gap_in_the_finer_tile_does_not_erase_the_coarser_one():
    """A finer tile's NaN is 'no data here', not 'erase what is under it'."""
    coarse = (9, 8895, 6994)
    fine = (10, 17790, 13988)
    south, west, north, east = gggs.grid_bounds(*fine)
    zoom = 17
    x, y = gggs.lonlat_to_tile(zoom, (west + east) / 2.0,
                               (south + north) / 2.0)

    empty_fine = numpy.full((960, 960), numpy.nan, dtype=numpy.float32)
    tiles = {coarse: _uniform_tile(*coarse, value=-10.0), fine: empty_fine}
    out = _Sampler(tiles, zoom)._sample_tile(x, y)
    covered = numpy.isfinite(out)
    assert covered.any(), 'the coarse coverage was erased by an empty finer tile'
    assert numpy.allclose(out[covered], -10.0)


def _latitude_grids(level, south, west, north, east):
    """Return grids over a box whose every cell holds its own centre latitude.

    Sampling such a cache makes the output image self-describing: pixel row r
    comes back holding the latitude the sampler actually read it from, to
    within half a cell.
    """
    indices = set()
    for lat in numpy.linspace(south + 1e-6, north - 1e-6, 50):
        for lon in numpy.linspace(west + 1e-6, east - 1e-6, 50):
            indices.add(gggs.grid_index_for(level, float(lat), float(lon)))
    grids = {}
    for row, col in indices:
        g_south, _, g_north, _ = gggs.grid_bounds(level, row, col)
        cells = gggs.CELL_ROWS_PER_GRID
        centres = g_south + ((numpy.arange(cells) + 0.5)
                             * (g_north - g_south) / cells)
        grids[(level, row, col)] = numpy.repeat(
            centres[:, None], gggs.CELL_COLUMNS_PER_GRID,
            axis=1).astype(numpy.float32)
    return grids


def test_image_rows_are_sampled_on_mercator_latitudes():
    """The sampler must place rows by inverting the projection, not evenly.

    A slippy tile is linear in Mercator y, not in latitude. Spacing the 256
    rows evenly in latitude reads every row from ground it does not cover --
    sub-pixel at zoom 15, but the coarse zooms the `zoom` parameter admits are
    the whole justification for the fix, and there the error is several pixel
    rows of vertical stretch.

    `gggs.tile_pixel_latitudes` is pinned in isolation elsewhere; this pins
    that `_sample_tile` actually USES it. Reverting the call site to even
    latitude interpolation left the suite green.

    Zoom 5 near 43 N: the two spacings differ by up to 0.14 deg, about 17
    level-0 cells and 4.5 pixel rows, so the two hypotheses are far apart
    compared with the half-cell quantisation of the answer.
    """
    zoom = 5
    x, y = gggs.lonlat_to_tile(zoom, -70.0, 43.0)
    south, west, north, east = gggs.tile_bounds(zoom, x, y)
    grids = _latitude_grids(0, south, west, north, east)
    out = _Sampler(grids, zoom)._sample_tile(x, y)

    sampled = numpy.nanmedian(out, axis=1)
    covered = numpy.isfinite(sampled)
    assert covered.all(), 'the synthetic cache did not cover the whole tile'

    mercator = numpy.array(gggs.tile_pixel_latitudes(zoom, y, 256))
    even = north - (numpy.arange(256) + 0.5) * (north - south) / 256.0
    half_cell = gggs.grid_angular_span(0) / gggs.CELL_ROWS_PER_GRID / 2.0

    from_mercator = numpy.max(numpy.abs(sampled - mercator))
    from_even = numpy.max(numpy.abs(sampled - even))
    assert from_mercator <= half_cell * 1.01, (
        'image rows are {:.4f} deg away from the Mercator row latitudes -- '
        'more than the half-cell {:.4f} deg the sampling quantises to, so '
        'the rows are not placed by inverting the projection'
        .format(from_mercator, half_cell))
    assert from_even > half_cell * 4, (
        'even-latitude spacing is indistinguishable from Mercator spacing at '
        'this zoom ({:.4f} deg apart) -- the test would pass against the bug'
        .format(from_even))


def test_image_columns_are_sampled_at_pixel_centres():
    """Longitudes must be the pixel CENTRES, not the pixel left edges.

    The `+ 0.5` on `lons` is a half-pixel shift of the whole image. It is small
    but systematic, and it was unbound: dropping it left the suite green.

    Zoom 15 against a level-10 grid: a pixel is about 5 cells wide, so half a
    pixel is ~2.6 cells -- resolvable against the one-cell quantisation.
    """
    level = 10
    row, col = gggs.grid_index_for(level, 43.07, -70.71)
    g_south, g_west, g_north, g_east = gggs.grid_bounds(level, row, col)
    cells = gggs.CELL_COLUMNS_PER_GRID
    centres = g_west + (numpy.arange(cells) + 0.5) * (g_east - g_west) / cells
    grid = numpy.repeat(centres[None, :], gggs.CELL_ROWS_PER_GRID,
                        axis=0).astype(numpy.float32)

    zoom = 15
    x, y = gggs.lonlat_to_tile(zoom, (g_west + g_east) / 2.0,
                               (g_south + g_north) / 2.0)
    south, west, north, east = gggs.tile_bounds(zoom, x, y)
    out = _Sampler({(level, row, col): grid}, zoom)._sample_tile(x, y)

    columns = numpy.flatnonzero(numpy.isfinite(out).any(axis=0))
    assert len(columns) > 32, 'too few covered columns to judge the offset'
    sampled = numpy.array([numpy.nanmedian(out[:, c]) for c in columns])

    span = (east - west) / 256.0
    cell = (g_east - g_west) / cells
    pixel_centres = west + (columns + 0.5) * span
    pixel_edges = west + columns * span

    from_centres = numpy.max(numpy.abs(sampled - pixel_centres))
    from_edges = numpy.max(numpy.abs(sampled - pixel_edges))
    assert from_centres <= cell, (
        'columns are sampled {:.2e} deg from the pixel centres, more than '
        'the {:.2e} deg cell they quantise to -- the half-pixel offset is '
        'gone'.format(from_centres, cell))
    assert from_edges > cell * 2, (
        'pixel centres and pixel edges are indistinguishable here ({:.2e} '
        'deg apart, cell {:.2e}) -- the test would pass against the bug'
        .format(from_edges, cell))
