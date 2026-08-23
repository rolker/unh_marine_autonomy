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

"""Pin the depth colouring, including the chart-datum reference.

The bug this file exists for: the `depth` band carries z in the MAP frame --
ellipsoidal -- not depth below chart datum. Colouring it directly saturated a
0-40 m ramp at every cell and painted 97% of the coverage the deepest colour.
It looked like a deep channel, not like a bug, and survived review; the
operator asked whether it was a datum problem.

The offset is the tide, so it moves. These tests pin the sign of the
correction, that it comes from TF rather than a constant, that a moving tide
invalidates the rendered mosaic, and that a missing transform stops the render
instead of painting a plausible-looking wrong answer.
"""

import threading

from marine_web_view import coverage_renderer
from marine_web_view.coverage_renderer import colour_table
from marine_web_view.coverage_renderer import CoverageRenderer
from marine_web_view.coverage_renderer import MAX_DEPTH
from marine_web_view.coverage_renderer import ramp_colour

import numpy

import tf2_ros


class _Logger:
    """Collect log lines instead of printing them."""

    def __init__(self):
        self.lines = []

    def warn(self, message, **kwargs):
        """Record a line."""
        self.lines.append(message)

    info = warn
    debug = warn
    error = warn


class _Translation:
    """Stand-in for a TF translation."""

    def __init__(self, z):
        self.z = z


class _Transform:
    """Stand-in for a TF transform."""

    def __init__(self, z):
        self.transform = type('T', (), {'translation': _Translation(z)})()


class _Buffer:
    """Stand-in TF buffer serving a scripted offset."""

    def __init__(self, z):
        self.z = z
        self.lookups = 0

    def lookup_transform(self, target, source, when):
        """Return the scripted transform, or raise if there is none."""
        self.lookups += 1
        if self.z is None:
            raise tf2_ros.LookupException('no transform')
        return _Transform(self.z)


class _Renderer:
    """Minimal stand-in exposing the colour path over a hand-built cache."""

    def __init__(self, z=-28.03, threshold=0.15):
        self.zoom = 15
        self._colours = colour_table()
        self._tiles = {}
        self._dirty = set()
        self._lock = threading.Lock()
        self._datum_offset = None
        self._tf_buffer = _Buffer(z)
        self.map_frame = 'ben/map'
        self.chart_datum_frame = 'ben/chart_datum'
        self.tide_invalidate_threshold = threshold
        self._logger = _Logger()

    def get_logger(self):
        """Return the collecting logger."""
        return self._logger

    _colourise = CoverageRenderer._colourise
    _update_datum_offset = CoverageRenderer._update_datum_offset
    _mark_dirty = CoverageRenderer._mark_dirty


def _sampled(*values):
    """Return a 256x256 tile whose first cells hold the given values."""
    tile = numpy.full((256, 256), numpy.nan, dtype=numpy.float32)
    for i, value in enumerate(values):
        tile[0, i] = value
    return tile


def test_the_ramp_runs_deep_to_shallow():
    """Fraction 0 is the deepest stop and 1 the shallowest."""
    assert ramp_colour(0.0) == coverage_renderer.RAMP[0]
    assert ramp_colour(1.0) == coverage_renderer.RAMP[-1]
    assert ramp_colour(-5.0) == ramp_colour(0.0), 'must clamp, not index'
    assert ramp_colour(5.0) == ramp_colour(1.0)
    # Blue falls and green rises from deep to shallow.
    deep, shallow = ramp_colour(0.0), ramp_colour(1.0)
    assert shallow[1] > deep[1] and shallow[2] > deep[2]


def test_the_colour_table_is_indexed_deepest_last():
    """Index 0 is the surface end of the scale, 255 the deep end."""
    table = colour_table()
    assert table.shape == (256, 3)
    assert table[0].tolist() != table[255].tolist()
    # Entry 255 is MAX_DEPTH, i.e. ramp fraction 0 -- the deepest stop.
    assert abs(int(table[255][2]) - int(table[0][2])) > 40


def test_depth_is_measured_down_from_chart_datum():
    """A map-frame z must be coloured as (datum_z - z), not as |z|.

    -35.98 m in the map frame with chart datum at -28.03 m is 7.95 m of
    water, which is inside the 0-40 m ramp. Taking the magnitude instead
    gives 35.98 m, nearly the bottom of the scale -- the observed bug.
    """
    node = _Renderer()
    datum_z = -28.03
    rgba = node._colourise(_sampled(-35.98, -57.23), datum_z)
    table = colour_table()

    for column, z in ((0, -35.98), (1, -57.23)):
        depth = datum_z - z
        expected = table[int(depth / MAX_DEPTH * 255.0)]
        assert rgba[0, column, :3].tolist() == expected.tolist(), z
        assert rgba[0, column, 3] == 255

    # Not the uncorrected reading: both would saturate to the deepest entry.
    saturated = table[255].tolist()
    assert rgba[0, 0, :3].tolist() != saturated
    assert rgba[0, 1, :3].tolist() != saturated


def test_uncovered_cells_stay_transparent():
    """Uncovered cells are the coverage signal and must never be painted."""
    node = _Renderer()
    rgba = node._colourise(_sampled(-35.98), -28.03)
    assert rgba[0, 0, 3] == 255
    assert rgba[0, 1, 3] == 0
    assert rgba[5, 5, 3] == 0
    assert numpy.count_nonzero(rgba[..., 3]) == 1


def test_an_all_empty_tile_colours_nothing():
    """The all-NaN case must not index the table at all."""
    node = _Renderer()
    rgba = node._colourise(
        numpy.full((256, 256), numpy.nan, dtype=numpy.float32), -28.03)
    assert not rgba.any()


def test_depth_is_clamped_to_the_scale_at_both_ends():
    """Above datum and beyond MAX_DEPTH must clamp, not wrap."""
    node = _Renderer()
    table = colour_table()
    rgba = node._colourise(_sampled(10.0, -1000.0), -28.03)
    assert rgba[0, 0, :3].tolist() == table[0].tolist()
    assert rgba[0, 1, :3].tolist() == table[255].tolist()


def test_the_offset_comes_from_tf_not_a_constant():
    """The first pass must read TF and adopt what it says."""
    node = _Renderer(z=-28.03)
    assert node._update_datum_offset()
    assert node._datum_offset == -28.03
    assert node._tf_buffer.lookups == 1


def test_a_moving_tide_invalidates_the_rendered_mosaic():
    """Past the threshold, every cached tile must be re-rendered."""
    node = _Renderer(z=-28.03, threshold=0.15)
    node._update_datum_offset()
    node._tiles[(10, 17801, 13988)] = None
    node._dirty.clear()

    node._tf_buffer.z = -28.10           # 0.07 m: below the threshold
    node._update_datum_offset()
    assert node._datum_offset == -28.03, 'jitter must not move the offset'
    assert not node._dirty, 'jitter must not re-render the whole mosaic'

    node._tf_buffer.z = -27.50           # 0.53 m of tide
    node._update_datum_offset()
    assert node._datum_offset == -27.50
    assert node._dirty, 'a real tide change must invalidate the mosaic'


def test_no_transform_means_no_render_rather_than_a_wrong_one():
    """Without a reference, refuse: the wrong answer looks plausible."""
    node = _Renderer(z=None)
    assert not node._update_datum_offset()
    assert node._datum_offset is None
    assert node._logger.lines

    # Once seen, a transient TF gap keeps the last known offset instead of
    # stalling the display.
    node._tf_buffer.z = -28.03
    assert node._update_datum_offset()
    node._tf_buffer.z = None
    assert node._update_datum_offset()
    assert node._datum_offset == -28.03


def test_a_disabled_correction_renders_unreferenced():
    """An empty chart_datum_frame is the documented opt-out."""
    node = _Renderer()
    node._tf_buffer = None
    assert node._update_datum_offset()
