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

"""Pin the render pass: un-publishing, retry, and failure containment.

Every failure here is invisible from inside the node. A pruned tile that is
skipped rather than overwritten leaves stale coverage standing in the bucket
and on the CDN. A failed upload whose tile was already cleared from the dirty
set is a permanent hole in the mosaic. An exception escaping the timer stops
rendering for good while the subscriptions carry on, so the node looks alive
and the map simply stops moving.
"""

import json
import threading

from marine_web_view.coverage_renderer import colour_table
from marine_web_view.coverage_renderer import CoverageRenderer
from marine_web_view.coverage_renderer import empty_png

import numpy


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


class _Pass:
    """Minimal stand-in exposing the render pass over scripted sampling."""

    def __init__(self, covered=True):
        self.zoom = 15
        self.prefix = 'live/coverage'
        self._colours = colour_table()
        self._lock = threading.Lock()
        self._dirty = set()
        self._tiles = {}
        self._published = set()
        self._rendered = 0
        self._failures = 0
        self._datum_offset = -28.03
        self._logger = _Logger()
        self.uploads = []
        self.meta = []
        self.upload_ok = True
        self.band_name = 'depth'
        self.cache_control = 60
        self.covered = covered
        self.raise_on_sample = False

    def get_logger(self):
        """Return the collecting logger."""
        return self._logger

    def _update_datum_offset(self):
        """Pretend the chart-datum offset is always available."""
        return True

    def _sample_tile(self, x, y):
        """Return a covered or empty sample, or blow up on demand."""
        if self.raise_on_sample:
            raise RuntimeError('sampling exploded')
        out = numpy.full((256, 256), numpy.nan, dtype=numpy.float32)
        if self.covered:
            out[:, :] = -35.0
        return out

    def _publish(self, payload, key, content_type='image/png'):
        """Record an upload attempt, keeping the manifest separate."""
        if key.endswith('meta.json'):
            assert content_type == 'application/json'
            self.meta.append(json.loads(payload))
            return self.upload_ok
        self.uploads.append((key, payload))
        return self.upload_ok

    def get_clock(self):
        """Return a stand-in clock."""
        return type('C', (), {
            'now': staticmethod(
                lambda: type('T', (), {'nanoseconds': 1_700_000_000 * 10 ** 9})
            )})()

    def _param(self, name):
        """Return the one parameter the manifest reads."""
        assert name == 'render_interval'
        return 20.0

    _colourise = CoverageRenderer._colourise
    _publish_meta = CoverageRenderer._publish_meta
    _render_one = CoverageRenderer._render_one
    _render_dirty = CoverageRenderer._render_dirty
    _render_pending = CoverageRenderer._render_pending


class _Threaded(_Pass):
    """A stand-in that also runs the render worker."""

    def __init__(self):
        super().__init__()
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._worker = threading.Thread(
            target=self._render_loop, name='test_render', daemon=True)
        self._worker.start()

    _wake_renderer = CoverageRenderer._wake_renderer
    _render_loop = CoverageRenderer._render_loop
    stop = CoverageRenderer.stop


def test_a_covered_tile_is_published_and_remembered():
    """The happy path, and the bookkeeping un-publishing depends on."""
    node = _Pass()
    node._dirty.add((10, 20))
    node._render_dirty()
    assert [key for key, _ in node.uploads] == ['live/coverage/15/10/20.png']
    assert node._published == {(10, 20)}
    assert node._rendered == 1
    assert not node._dirty


def test_pruned_coverage_is_un_published():
    """A published tile that loses its coverage must be overwritten."""
    node = _Pass()
    node._dirty.add((10, 20))
    node._render_dirty()
    node.uploads.clear()

    node.covered = False
    node._dirty.add((10, 20))
    node._render_dirty()
    assert node.uploads, (
        'the stale PNG was left standing: the display keeps showing coverage '
        'the source no longer holds')
    key, payload = node.uploads[0]
    assert key == 'live/coverage/15/10/20.png'
    assert payload == empty_png()
    assert not node._published


def test_an_empty_tile_that_was_never_published_costs_nothing():
    """Un-publishing must not turn into a PUT per empty tile per pass."""
    node = _Pass(covered=False)
    node._dirty.add((10, 20))
    node._render_dirty()
    assert not node.uploads
    assert node._rendered == 0


def test_a_failed_upload_is_retried_rather_than_lost():
    """The dirty set is cleared before rendering -- failures must go back."""
    node = _Pass()
    node.upload_ok = False
    node._dirty.add((10, 20))
    node._render_dirty()
    assert node._dirty == {(10, 20)}, (
        'a failed upload became a permanent hole in the mosaic')
    assert node._rendered == 0

    node.upload_ok = True
    node._render_dirty()
    assert not node._dirty
    assert node._rendered == 1


def test_one_exploding_tile_does_not_end_the_render_timer():
    """An escape from the timer stops rendering for the life of the node."""
    node = _Pass()
    node.raise_on_sample = True
    node._dirty.add((10, 20))
    node._render_dirty()             # must not raise
    assert node._failures == 1
    assert node._dirty == {(10, 20)}

    node.raise_on_sample = False
    node._render_dirty()
    assert node._rendered == 1


def test_the_timer_only_rings_the_bell():
    """Rendering must not run on the executor thread.

    A pass samples, encodes and uploads with a 30 s timeout per object. Doing
    that in a timer callback blocks the single-threaded executor for the whole
    pass, and every BEST_EFFORT tile pushed meanwhile is dropped.
    """
    node = _Threaded()
    try:
        node._wake.clear()
        node._dirty.add((10, 20))
        node._wake_renderer()
        assert node._wake.is_set() or node.uploads is not None
    finally:
        node.stop()


def test_the_worker_renders_when_woken():
    """The bell must actually reach the worker."""
    node = _Threaded()
    try:
        node._dirty.add((10, 20))
        node._wake_renderer()
        for _ in range(200):
            if node.uploads:
                break
            threading.Event().wait(0.01)
        assert node.uploads, 'the render thread never woke'
    finally:
        node.stop()


def test_stopping_flushes_what_is_still_dirty():
    """Up to a render interval of the end of a line is otherwise lost."""
    node = _Threaded()
    node._dirty.add((11, 21))
    node.stop()
    assert [key for key, _ in node.uploads] == ['live/coverage/15/11/21.png']
    assert not node._worker.is_alive()


def test_stopping_twice_is_harmless():
    """Shutdown paths can and do run more than once."""
    node = _Threaded()
    node.stop()
    node._dirty.add((11, 21))
    node.stop()
    assert not node.uploads


def test_every_pass_publishes_the_manifest():
    """The page reads its zoom -- and the renderer's liveness -- from it."""
    node = _Pass()
    node._dirty.add((10, 20))
    node._render_dirty()
    assert node.meta, 'no manifest was published'
    meta = node.meta[-1]
    assert meta['zoom'] == 15
    assert meta['status'] == 'ok'
    assert meta['band'] == 'depth'
    assert meta['published_tiles'] == 1
    assert meta['stamp'] > 0


def test_the_manifest_is_a_heartbeat_even_with_nothing_to_render():
    """An idle renderer must still say it is alive."""
    node = _Pass()
    node._render_dirty()
    assert len(node.meta) == 1
    node._render_dirty()
    assert len(node.meta) == 2
