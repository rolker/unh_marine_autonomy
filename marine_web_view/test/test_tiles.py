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

"""Pin the VisualizationBand decode contract (project ADR-0008 D1).

Both rules pinned here fail silently rather than loudly when broken: a
hardcoded element type mis-decodes three of the four band types into
plausible-looking garbage, and comparing the nodata sentinel after
dequantization turns "no data" into a very deep sounding.
"""

import struct

from marine_web_view import tiles

import numpy
import pytest


def _pack(dtype_code, values):
    """Pack values little-endian for the given VisualizationBand dtype."""
    fmt = {tiles.UINT8: '<{}B', tiles.INT16: '<{}h',
           tiles.UINT16: '<{}H'}[dtype_code]
    return struct.pack(fmt.format(len(values)), *values)


def test_dequantizes_generically_for_every_dtype():
    """Dequantize as raw * scale + offset, element type from dtype."""
    cases = ((tiles.UINT8, [0, 1, 250]), (tiles.INT16, [-100, 0, 1000]),
             (tiles.UINT16, [0, 7, 60000]))
    for dtype_code, values in cases:
        out = tiles.decode_band(dtype_code, _pack(dtype_code, values),
                                len(values), 1, 0.01, -5.0, nodata=-32768)
        expected = [v * 0.01 - 5.0 for v in values]
        numpy.testing.assert_allclose(out[0], expected, rtol=1e-5)


def test_int16_is_not_assumed():
    """A UINT8 band must not be read as int16.

    Hardcoding int16 is the tempting shortcut -- depth is the only band in v1
    bathy -- and it would silently halve the cell count and pair up bytes.
    """
    values = [1, 2, 3, 4]
    out = tiles.decode_band(tiles.UINT8, _pack(tiles.UINT8, values),
                            4, 1, 1.0, 0.0, nodata=255)
    assert out.shape == (1, 4)
    numpy.testing.assert_allclose(out[0], values)


def test_nodata_is_compared_in_raw_units():
    """Catch the sentinel before scale/offset are applied.

    -32768 raw with scale 0.01 dequantizes to -327.68, which is a perfectly
    plausible depth. If the comparison happened after dequantization the cell
    would render as very deep water instead of a hole.
    """
    out = tiles.decode_band(tiles.INT16, _pack(tiles.INT16, [-32768, 500]),
                            2, 1, 0.01, 0.0, nodata=-32768)
    assert numpy.isnan(out[0, 0]), 'sentinel was not recognised'
    numpy.testing.assert_allclose(out[0, 1], 5.0)


def test_rejects_unknown_dtype():
    """An unknown element type must fail loudly, not guess."""
    with pytest.raises(ValueError):
        tiles.decode_band(99, b'\x00\x00', 1, 1, 1.0, 0.0, nodata=0)


def test_rejects_truncated_buffer():
    """A short buffer must fail rather than render a shifted image."""
    with pytest.raises(ValueError):
        tiles.decode_band(tiles.INT16, _pack(tiles.INT16, [1, 2]),
                          4, 1, 1.0, 0.0, nodata=0)


def test_row_major_order():
    """Cells are row-major, so a 2x2 window must not come back transposed."""
    out = tiles.decode_band(tiles.UINT8, _pack(tiles.UINT8, [1, 2, 3, 4]),
                            2, 2, 1.0, 0.0, nodata=255)
    numpy.testing.assert_allclose(out, [[1, 2], [3, 4]])


def test_apply_window_patches_in_place():
    """A dirty sub-window patches into the tile at its offset."""
    tile = tiles.new_tile(4, 4)
    assert numpy.isnan(tile).all()
    values = numpy.array([[1.0, 2.0], [3.0, 4.0]], dtype=numpy.float32)
    tiles.apply_window(tile, values, window_row=1, window_col=2)
    numpy.testing.assert_allclose(tile[1:3, 2:4], values)
    assert numpy.isnan(tile[0, 0]), 'patch touched cells outside the window'


def test_apply_window_rejects_overflow():
    """A window running off the edge means a geometry disagreement."""
    tile = tiles.new_tile(4, 4)
    values = numpy.zeros((2, 2), dtype=numpy.float32)
    with pytest.raises(ValueError):
        tiles.apply_window(tile, values, window_row=3, window_col=3)


def test_time_to_nanoseconds_orders_versions():
    """Versions must collapse to one ordered scalar for newest-wins."""
    class _Stamp:

        def __init__(self, sec, nanosec):
            self.sec = sec
            self.nanosec = nanosec

    assert (tiles.time_to_nanoseconds(_Stamp(5, 0))
            > tiles.time_to_nanoseconds(_Stamp(4, 999999999)))
    assert tiles.time_to_nanoseconds(_Stamp(0, 0)) == 0
