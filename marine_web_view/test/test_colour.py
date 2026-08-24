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


class _Stamp:
    """Stand-in for a builtin_interfaces/Time."""

    def __init__(self, seconds):
        self.sec = int(seconds)
        self.nanosec = int(round((seconds - int(seconds)) * 1e9))


class _Transform:
    """Stand-in for a TF transform, carrying its own stamp like the real one."""

    def __init__(self, z, stamp_seconds=0.0):
        self.transform = type('T', (), {'translation': _Translation(z)})()
        self.header = type('H', (), {'stamp': _Stamp(stamp_seconds)})()


class _Buffer:
    """Stand-in TF buffer serving a scripted offset.

    `stamp` is the ROS time the served transform is stamped with. It does NOT
    advance on its own: that is the point. `lookup_transform(..., Time())`
    returns the LATEST AVAILABLE transform and tf2 prunes only on insert, so a
    publisher that dies keeps resolving the same transform forever.
    """

    def __init__(self, z, stamp=0.0):
        self.z = z
        self.stamp = stamp
        self.lookups = 0

    def lookup_transform(self, target, source, when):
        """Return the scripted transform, or raise if there is none."""
        self.lookups += 1
        if self.z is None:
            raise tf2_ros.LookupException('no transform')
        return _Transform(self.z, self.stamp)


class _Renderer:
    """Minimal stand-in exposing the colour path over a hand-built cache."""

    def __init__(self, z=-28.03, threshold=0.15, stamp=1_700_000_000.0):
        self.zoom = 15
        self.sim_clock_seconds = stamp
        self._colours = colour_table()
        self._tiles = {}
        self._dirty = set()
        self._lock = threading.Lock()
        self._datum_offset = None
        self._datum_stamp = None
        self._tf_buffer = _Buffer(z, stamp)
        self.map_frame = 'ben/map'
        self.chart_datum_frame = 'ben/chart_datum'
        self.tide_invalidate_threshold = threshold
        self._logger = _Logger()

    def get_logger(self):
        """Return the collecting logger."""
        return self._logger

    def get_clock(self):
        """Return a stand-in ROS clock reading `sim_clock_seconds`."""
        nanoseconds = int(self.sim_clock_seconds * 10 ** 9)
        return type('C', (), {
            'now': staticmethod(
                lambda: type('T', (), {'nanoseconds': nanoseconds})
            )})()

    _colourise = CoverageRenderer._colourise
    _datum_age = CoverageRenderer._datum_age
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


def _tinted(rgb):
    """Return a ramp colour with the coverage tint applied, as the table has."""
    return [min(255, int(round(channel * tint)))
            for channel, tint in zip(rgb, coverage_renderer.COVERAGE_TINT)]


def test_the_colour_table_is_indexed_deepest_last():
    """Index 0 is the surface end of the scale, 255 the deep end.

    Pinned to the exact endpoint colours, derived from `ramp_colour` rather
    than from `colour_table` itself. The previous form compared the blue
    channels' magnitude of difference, which is symmetric: inverting
    `colour_table()` left the whole suite green, and shallow water would have
    painted deepest-blue against the page legend.
    """
    table = colour_table()
    assert table.shape == (256, 3)
    # Entry 0 is depth 0 -- ramp fraction 1, the shallowest stop.
    assert table[0].tolist() == _tinted(ramp_colour(1.0))
    # Entry 255 is MAX_DEPTH -- ramp fraction 0, the deepest stop.
    assert table[255].tolist() == _tinted(ramp_colour(0.0))
    assert table[0].tolist() != table[255].tolist()


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


def test_a_dead_tide_publisher_ages_even_though_the_lookup_succeeds():
    """Staleness must be measured on the DATA, not on the lookup.

    `lookup_transform(..., Time())` asks for the latest available transform,
    and tf2 prunes its buffer only when something is inserted -- so a tide
    publisher that dies keeps resolving the same transform forever, with no
    exception, for as long as the node runs. Timing the lookup therefore
    reported a permanently fresh datum for a permanently frozen water level,
    and the manifest said `ok` while every tile was coloured against an old
    tide. That is the exact degradation `DATUM_STALE_SECONDS` exists to
    surface, so it is checked against the transform's own stamp.
    """
    start = 1_700_000_000.0
    node = _Renderer(z=-28.03, stamp=start)
    assert node._update_datum_offset()
    assert node._datum_age() < 1.0, 'a just-published transform is not old'

    # Time passes. The publisher is gone -- the buffer still answers, with the
    # same transform and the same stamp it had at `start`.
    for elapsed in (30.0, coverage_renderer.DATUM_STALE_SECONDS + 5.0, 3600.0):
        node.sim_clock_seconds = start + elapsed
        assert node._update_datum_offset(), 'the dead lookup still succeeds'
        assert node._tf_buffer.lookups > 1
        age = node._datum_age()
        assert abs(age - elapsed) < 1.0, (
            'the offset has been frozen for {:.0f} s but reads as {:.0f} s '
            'old -- the age is being taken from the lookup rather than from '
            'the transform'.format(elapsed, age))
    assert node._datum_age() > coverage_renderer.DATUM_STALE_SECONDS


def test_a_live_tide_publisher_stays_fresh():
    """A publisher that keeps stamping fresh transforms must not read stale."""
    start = 1_700_000_000.0
    node = _Renderer(z=-28.03, stamp=start)
    for elapsed in (0.0, 30.0, 600.0, 7200.0):
        node.sim_clock_seconds = start + elapsed
        node._tf_buffer.stamp = start + elapsed      # a live publisher
        assert node._update_datum_offset()
        assert node._datum_age() < 1.0, (
            'a transform stamped now reads as {:.0f} s old'
            .format(node._datum_age()))


def test_a_static_chart_datum_has_no_age():
    """tf2 answers a Time() query on a static transform with a zero stamp.

    A static chart-datum publisher is a legitimate configuration -- water with
    no tide correction to apply -- and a zero stamp read literally is 1970, so
    it would dim the layer forever. A static datum cannot go stale, so it has
    no age at all.
    """
    node = _Renderer(z=-28.03, stamp=0.0)
    assert node._update_datum_offset()
    assert node._datum_offset == -28.03
    assert node._datum_age() is None
