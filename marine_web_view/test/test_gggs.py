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

"""Pin the GGGS geometry against the authoritative C++ spec.

gggs.py is a cross-language duplicate of correctness-critical math that lives
in marine_autonomy/include/marine_autonomy/gggs/. Nothing links the two, so a
change on the C++ side would drift silently and mis-georeference coverage
tiles. These tests fail loudly instead.

The level-10 case below is a real tile index observed on
`<ns>/sensors/mbes/cube_bathymetry/coverage_catalog` in simulation, with the
vessel surveying inside it -- so it pins the math against data the system
actually produced, not against a value recomputed from the same formulas.
"""

import math

from marine_web_view import gggs


def test_level_0_geometry_closes():
    """Level-0 rows must span 192 deg of latitude and columns 360 of longitude."""
    assert gggs.row_count(0) == 24
    assert gggs.grid_angular_span(0) == 8.0
    assert gggs.row_count(0) * gggs.grid_angular_span(0) == 192.0
    equatorial_row = 12
    assert (gggs.column_count(0, equatorial_row)
            * gggs.grid_longitudinal_span(0, equatorial_row)) == 360.0


def test_span_halves_each_level():
    """Each level halves the angular span."""
    for level in range(0, 12):
        assert gggs.grid_angular_span(level) == 8.0 / (1 << level)


def test_observed_catalog_tile_brackets_the_vessel():
    """A real L10 catalog tile must bracket where the vessel was surveying."""
    south, west, north, east = gggs.grid_bounds(10, 17801, 13988)
    # One level-10 grid spans 8/1024 deg.
    assert math.isclose(north - south, 8.0 / 1024, rel_tol=1e-9)
    assert math.isclose(east - west, 8.0 / 1024, rel_tol=1e-9)
    # Observed extent, to 5 dp.
    assert round(south, 5) == 43.07031
    assert round(west, 5) == -70.71875


def test_scale_factor_boundaries_are_exact():
    """Pin the +/-72 and +/-80 deg band edges, from both spellings.

    A round-trip through grid_index_for cannot pin these: it samples whatever
    band the latitude happens to land in, so an edge moved by a whole grid
    still round-trips. These assert the edge itself, against the two C++
    functions being ported -- the row-indexed `LevelSpecs::latitudeScaleFactor`
    and the latitude-indexed free function in core.h, which disagree by design
    within one grid of an edge.
    """
    # `core.h:96` compares abs(latitude) with STRICT less-than, so a latitude
    # exactly on an edge takes the poleward band.
    for latitude, expected in ((-80.001, 9), (-80.0, 9), (-79.999, 3),
                               (-72.0, 3), (-71.999, 1), (0.0, 1),
                               (71.999, 1), (72.0, 3), (79.999, 3),
                               (80.0, 9)):
        assert gggs.scale_factor_for_latitude(latitude) == expected, latitude

    # Row-indexed: the C++ compares `row >= row_minus_72 && row < row_plus_72`,
    # so the row starting at an edge is inside the band at the southern edges
    # and outside it at the northern ones. Both the edge row and the row below
    # it are pinned -- an edge off by one grid moves only one of them.
    for level in (0, 5, 10):
        span = gggs.grid_angular_span(level)
        for edge, at_edge, below_edge in ((-80.0, 3, 9), (-72.0, 1, 3),
                                          (72.0, 3, 1), (80.0, 9, 3)):
            edge_row = int((edge - gggs.LATITUDE_ORIGIN) / span)
            assert gggs.latitude_scale_factor(level, edge_row) == at_edge, (
                level, edge)
            assert gggs.latitude_scale_factor(
                level, edge_row - 1) == below_edge, (level, edge)


def test_column_count_divides_the_row_evenly():
    """Columns must tile a full 360 deg at every scale factor."""
    for level in (0, 3, 10):
        for row in (0, 1, gggs.row_count(level) // 2,
                    gggs.row_count(level) - 1):
            span = gggs.grid_longitudinal_span(level, row)
            assert gggs.column_count(level, row) * span == 360.0, (level, row)


def test_longitude_is_wrapped_not_run_off_the_end():
    """A longitude past +/-180 must wrap rather than index past the row."""
    assert gggs.normalize_longitude(185.0) == -175.0
    assert gggs.normalize_longitude(-185.0) == 175.0
    assert gggs.normalize_longitude(180.0) == -180.0
    row, column = gggs.grid_index_for(10, 43.075, -70.699 + 360.0)
    assert (row, column) == gggs.grid_index_for(10, 43.075, -70.699)
    assert column < gggs.column_count(10, row)


def test_latitude_overshoot_clamps_but_nonsense_raises():
    """Mirror the C++ epsilon clamp and its out_of_range throw."""
    row, _ = gggs.grid_index_for(10, 90.0 + 1e-9, 0.0)
    assert row < gggs.row_count(10)
    for bad in (91.0, -91.0):
        try:
            gggs.grid_index_for(10, bad, 0.0)
        except ValueError:
            continue
        raise AssertionError('latitude {} should be rejected'.format(bad))


def test_tiles_covering_treats_the_extent_as_half_open():
    """The north and east edges belong to the next grid, not this one."""
    zoom = 14
    # A box whose edges land exactly on slippy-tile boundaries: the covering
    # set must be the interior tiles only.
    x, y = 4900, 6000
    south, west, north, east = gggs.tile_bounds(zoom, x, y)
    tiles = set(gggs.tiles_covering(zoom, south, west, north, east))
    assert tiles == {(x, y)}, (
        'an extent that is exactly one tile must cover exactly that tile, '
        'not a spurious row and column beyond it: got {}'.format(sorted(tiles)))

    # Two tiles wide and one tall.
    _, _, _, east2 = gggs.tile_bounds(zoom, x + 1, y)
    assert set(gggs.tiles_covering(zoom, south, west, north, east2)) == {
        (x, y), (x + 1, y)}

    # Degenerate boxes yield nothing rather than one arbitrary tile.
    assert list(gggs.tiles_covering(zoom, south, west, south, east)) == []
    assert list(gggs.tiles_covering(zoom, south, west, north, west)) == []


def test_grid_extent_covers_only_its_own_tiles():
    """A real L10 grid must not mark the tiles beyond its open edges dirty."""
    south, west, north, east = gggs.grid_bounds(10, 17801, 13988)
    zoom = 15
    tiles = set(gggs.tiles_covering(zoom, south, west, north, east))
    for x, y in tiles:
        t_south, t_west, t_north, t_east = gggs.tile_bounds(zoom, x, y)
        assert t_south < north and t_north > south, (x, y)
        assert t_west < east and t_east > west, (x, y)


def test_position_round_trips_through_index():
    """A position must fall inside the bounds of the grid it indexes to.

    Covers all three polar scale-factor bands: the 3x and 9x branches are dead
    code for coastal work but are part of the ported spec, so they are pinned
    rather than left to rot untested.
    """
    cases = ((43.075, -70.699, 1), (0.0, 0.0, 1), (-45.0, 170.0, 1),
             (75.0, -30.0, 3), (85.0, 10.0, 9), (-78.0, 45.0, 3),
             (-88.0, -120.0, 9))
    for latitude, longitude, expected_scale in cases:
        row, column = gggs.grid_index_for(10, latitude, longitude)
        assert gggs.latitude_scale_factor(10, row) == expected_scale, (
            'scale factor at {} deg'.format(latitude))
        south, west, north, east = gggs.grid_bounds(10, row, column)
        assert south <= latitude <= north, 'lat {}'.format(latitude)
        assert west <= longitude <= east, 'lon {}'.format(longitude)


def test_latitude_is_clamped_at_the_poles():
    """Rows run to +/-96 deg but geographic latitude stops at +/-90."""
    south, _, north, _ = gggs.grid_bounds(0, 0, 0)
    assert south == -90.0
    top = gggs.row_count(0) - 1
    _, _, north_top, _ = gggs.grid_bounds(0, top, 0)
    assert north_top == 90.0


def test_slippy_tile_round_trip():
    """A position must fall inside the slippy tile it maps to."""
    for zoom in (10, 13, 16):
        for latitude, longitude in ((43.075, -70.699), (0.0, 0.0),
                                    (-33.9, 151.2)):
            x, y = gggs.lonlat_to_tile(zoom, longitude, latitude)
            south, west, north, east = gggs.tile_bounds(zoom, x, y)
            assert south <= latitude <= north
            assert west <= longitude <= east


def test_tiles_covering_includes_the_corners():
    """A covering set must include the tiles holding each corner."""
    south, west, north, east = 43.05, -70.75, 43.10, -70.65
    tiles = set(gggs.tiles_covering(14, south, west, north, east))
    assert gggs.lonlat_to_tile(14, west, north) in tiles
    assert gggs.lonlat_to_tile(14, east, south) in tiles
    assert len(tiles) > 1


def test_pixel_rows_are_spaced_in_mercator_not_in_latitude():
    """A slippy tile is linear in Mercator y, so its rows must be too.

    Spacing rows evenly in latitude draws each one at a latitude it does not
    cover -- a vertical stretch of the image. This pins the row centres to the
    inverse projection and shows the linear-in-latitude approximation is a
    real error at the coarse zooms the renderer's `zoom` parameter admits.
    """
    zoom, y = 4, 5
    south, _, north, _ = gggs.tile_bounds(zoom, 0, y)
    lats = gggs.tile_pixel_latitudes(zoom, y, 256)

    assert len(lats) == 256
    assert lats == sorted(lats, reverse=True), 'rows must run north to south'
    assert north > lats[0] > lats[-1] > south, (
        'every row centre must sit inside the tile')

    linear = [north - (row + 0.5) * (north - south) / 256.0
              for row in range(256)]
    worst = max(abs(a - b) for a, b in zip(lats, linear))
    assert worst > 0.01, (
        'expected the two spacings to differ measurably at zoom 4; got '
        '{} deg -- has the projection been linearised?'.format(worst))


def test_pixel_rows_are_consistent_with_the_tile_they_sample():
    """Each row centre must index back to the tile it came from.

    At zoom 15 -- the shipped render level -- the Mercator and linear spacings
    agree to well under a pixel, so this is the check that the new spacing did
    not move the sampling somewhere else entirely.
    """
    zoom, y = 15, 12000
    for latitude in gggs.tile_pixel_latitudes(zoom, y, 256):
        assert gggs.lonlat_to_tile(zoom, -70.8, latitude)[1] == y
