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
