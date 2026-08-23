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

"""Pin what the tile-ingest path refuses to do with a bad message.

Every case here is a message the transport allows and the node used to act
on: an index that is not a real grid, dimensions that are merely large
uint16s, and a grid coarse enough that enumerating its slippy tiles is itself
the denial of service. None of them fail loudly on their own -- the node
either dies allocating or wedges enumerating -- so they are pinned here.

The node is exercised through a stand-in rather than a live rclpy Node: these
are pure functions of the message, and spinning a node would test the
executor, not the guard.
"""

import threading

from marine_web_view import coverage_renderer
from marine_web_view.coverage_renderer import CoverageRenderer
from marine_web_view.reconciler import TileCatalogReconciler


class _Logger:
    """Collect log lines instead of printing them."""

    def __init__(self):
        self.lines = []

    def warn(self, message, **kwargs):
        """Record a warning."""
        self.lines.append(message)

    info = warn
    debug = warn
    error = warn


class _Index:
    """Stand-in for marine_interfaces/TileIndex."""

    def __init__(self, level, row, col):
        self.level = level
        self.row = row
        self.col = col


class _Band:
    """Stand-in for marine_interfaces/VisualizationBand."""

    def __init__(self, name, data, dtype=3, scale=0.01, offset=0.0,
                 nodata=-32768):
        self.name = name
        self.data = data
        self.dtype = dtype
        self.scale = scale
        self.offset = offset
        self.nodata = nodata


class _Stamp:
    """Stand-in for builtin_interfaces/Time."""

    def __init__(self, sec, nanosec=0):
        self.sec = sec
        self.nanosec = nanosec


class _Header:
    """Stand-in for std_msgs/Header."""

    def __init__(self, stamp):
        self.stamp = stamp


class _Tile:
    """Stand-in for marine_interfaces/SonarVisualizationTile."""

    def __init__(self, level=10, row=17801, col=13988, width=4, height=4,
                 window_row=0, window_col=0, window_width=None,
                 window_height=None, seconds=100, values=None):
        self.index = _Index(level, row, col)
        self.width = width
        self.height = height
        self.window_row = window_row
        self.window_col = window_col
        self.window_width = window_width if window_width is not None else width
        self.window_height = (window_height if window_height is not None
                              else height)
        self.header = _Header(_Stamp(seconds))
        cells = self.window_width * self.window_height
        raw = values if values is not None else [100] * cells
        self.bands = [_Band('depth', b''.join(
            int(v).to_bytes(2, 'little', signed=True) for v in raw))]


class _Ingest:
    """Minimal stand-in exposing the ingest path over a hand-built cache."""

    def __init__(self, zoom=15):
        self.zoom = zoom
        self.band_name = 'depth'
        self._tiles = {}
        self._applied = {}
        self._dirty = set()
        self._lock = threading.Lock()
        self._reconciler = TileCatalogReconciler()
        self._failures = 0
        self._logger = _Logger()

    def get_logger(self):
        """Return the collecting logger."""
        return self._logger

    _tile_is_sane = CoverageRenderer._tile_is_sane
    _on_tile = CoverageRenderer._on_tile
    _mark_dirty = CoverageRenderer._mark_dirty


def test_a_well_formed_tile_is_accepted():
    """Guard the guards: the happy path must still land in the cache."""
    node = _Ingest()
    node._on_tile(_Tile())
    assert (10, 17801, 13988) in node._tiles
    assert node._dirty


def test_an_index_that_is_not_a_grid_is_refused():
    """A row or column past the end of its level indexes nothing."""
    for level, row, col in ((10, 17801, 46080), (10, 24576, 0),
                            (255, 0, 0), (10, 17801, -1)):
        node = _Ingest()
        node._on_tile(_Tile(level=level, row=row, col=col))
        assert not node._tiles, (level, row, col)
        assert node._logger.lines, 'refusal must be logged'


def test_absurd_dimensions_are_refused_before_allocating():
    """65535 x 65535 float32 is 17 GB, and both fields are uint16."""
    node = _Ingest()
    node._on_tile(_Tile(width=65535, height=65535, window_width=1,
                        window_height=1, values=[0]))
    assert not node._tiles
    assert 'implausible' in ' '.join(node._logger.lines)


def test_zero_dimensions_are_refused():
    """A zero-sized tile would leave a permanently empty cache entry."""
    for width, height in ((0, 4), (4, 0)):
        node = _Ingest()
        node._on_tile(_Tile(width=width, height=height, window_width=1,
                            window_height=1, values=[0]))
        assert not node._tiles, (width, height)


def test_the_edge_limit_is_the_stated_one():
    """One cell over MAX_TILE_EDGE is refused; the limit itself is not."""
    edge = coverage_renderer.MAX_TILE_EDGE
    node = _Ingest()
    node._on_tile(_Tile(width=edge + 1, height=1, window_width=1,
                        window_height=1, values=[0]))
    assert not node._tiles
    node = _Ingest()
    node._on_tile(_Tile(width=edge, height=1, window_width=1,
                        window_height=1, values=[0]))
    assert node._tiles


def test_a_coarse_grid_does_not_enumerate_the_planet():
    """A level-0 grid is ~1.6e7 slippy tiles at zoom 15 -- refuse it."""
    node = _Ingest()
    node._mark_dirty((0, 12, 13))
    assert not node._dirty
    assert 'spans more than' in ' '.join(node._logger.lines)


def test_a_level_10_grid_marks_a_handful_of_tiles():
    """The sane case must stay far below the bound."""
    node = _Ingest()
    node._mark_dirty((10, 17801, 13988))
    assert 0 < len(node._dirty) < coverage_renderer.MAX_DIRTY_TILES_PER_GRID


def test_a_malformed_band_is_dropped_not_cached():
    """A truncated patch must not leave a half-written tile behind."""
    node = _Ingest()
    tile = _Tile()
    tile.bands[0].data = tile.bands[0].data[:-2]
    node._on_tile(tile)
    assert node._failures == 1


def _values(constant, cells):
    """Return a raw INT16 buffer of one repeated value."""
    return [constant] * cells


def test_a_stale_patch_does_not_overwrite_fresh_cells():
    """Newest-wins must be enforced before the window is applied."""
    node = _Ingest()
    node._on_tile(_Tile(seconds=200, values=_values(2000, 16)))
    node._on_tile(_Tile(seconds=100, values=_values(500, 16)))
    tile = node._tiles[(10, 17801, 13988)]
    assert round(float(tile[0, 0]), 3) == 20.0, (
        'the older patch overwrote the newer one: newest-wins is not '
        'enforced before apply_window')


def test_a_newer_patch_does_overwrite():
    """Guard the guard: ordering must not block legitimate updates."""
    node = _Ingest()
    node._on_tile(_Tile(seconds=100, values=_values(500, 16)))
    node._on_tile(_Tile(seconds=200, values=_values(2000, 16)))
    assert round(float(node._tiles[(10, 17801, 13988)][0, 0]), 3) == 20.0


def test_a_partial_window_does_not_claim_possession():
    """A sub-window patch must leave the tile re-requestable.

    This is the whole self-healing story: a lost best-effort window has to be
    healed by the next catalog round. Claiming the catalog's version off a
    partial patch means the reconciler sees nothing to request and the hole
    is permanent.
    """
    index = (10, 17801, 13988)
    node = _Ingest()
    node._on_tile(_Tile(seconds=100, window_row=1, window_col=1,
                        window_width=2, window_height=2,
                        values=_values(500, 4)))
    assert index in node._tiles, 'the pixels must still be applied'
    assert not node._reconciler.has(index), (
        'a partial window claimed possession -- the catalog will never '
        're-request this tile and any lost window is permanent')
    to_request, _ = node._reconciler.reconcile([(index, 100 * 10 ** 9)], 0)
    assert to_request == [index]


def test_a_whole_tile_does_claim_possession():
    """A full-tile message is possession, and stops the re-request."""
    index = (10, 17801, 13988)
    node = _Ingest()
    node._on_tile(_Tile(seconds=100))
    assert node._reconciler.version_of(index) == 100 * 10 ** 9
    to_request, _ = node._reconciler.reconcile([(index, 100 * 10 ** 9)], 0)
    assert to_request == []


def test_a_window_that_does_not_fit_leaves_no_empty_tile():
    """A first patch that overruns must not poison the cache with NaN."""
    node = _Ingest()
    node._on_tile(_Tile(window_row=3, window_col=3, window_width=4,
                        window_height=4, values=_values(500, 16)))
    assert not node._tiles, (
        'an all-NaN entry renders as "no coverage" and blocks the '
        're-request that would fix it')
    assert node._failures == 1
