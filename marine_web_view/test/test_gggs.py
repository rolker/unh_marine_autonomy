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
