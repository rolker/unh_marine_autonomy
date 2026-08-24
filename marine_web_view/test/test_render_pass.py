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
import time

from marine_web_view import coverage_renderer
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
        self._failure_lock = threading.Lock()
        self._datum_offset = -28.03
        self._logger = _Logger()
        self.uploads = []
        self.meta = []
        self.upload_ok = True
        self.band_name = 'depth'
        self.cache_control = 60
        self.render_interval = 20.0
        self.covered = covered
        self.raise_on_sample = False
        self.sim_clock_seconds = 1_700_000_000.0
        self.datum_available = True
        # The correction is enabled (a real Buffer would sit here) and was
        # refreshed just now, so `_datum_age` reads as fresh by default.
        self._tf_buffer = object()
        self._datum_stamp = time.monotonic()

    def get_logger(self):
        """Return the collecting logger."""
        return self._logger

    def _update_datum_offset(self):
        """Report whether a chart-datum offset is held, on demand."""
        return self.datum_available

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
        """Return a stand-in ROS clock reading `sim_clock_seconds`."""
        nanoseconds = int(self.sim_clock_seconds * 10 ** 9)
        return type('C', (), {
            'now': staticmethod(
                lambda: type('T', (), {'nanoseconds': nanoseconds})
            )})()

    def _param(self, name):
        """Fail loudly: the render thread must not read parameters.

        `get_parameter` is the executor thread's to call, and the manifest was
        reaching for `render_interval` through it on every pass. The value is
        cached at construction now, so nothing on this path needs a parameter
        at all.
        """
        raise AssertionError(
            'the render pass read parameter {!r} off the executor '
            'thread'.format(name))

    _colourise = CoverageRenderer._colourise
    _datum_age = CoverageRenderer._datum_age
    _note_failure = CoverageRenderer._note_failure
    _publish_meta = CoverageRenderer._publish_meta
    _render_one = CoverageRenderer._render_one
    _render_dirty = CoverageRenderer._render_dirty
    _render_pending = CoverageRenderer._render_pending


class _Threaded(_Pass):
    """A stand-in that also runs the render worker, recording who renders."""

    def __init__(self):
        super().__init__()
        self.render_threads = set()
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._worker = threading.Thread(
            target=self._render_loop, name='test_render', daemon=True)
        self._worker.start()

    def _sample_tile(self, x, y):
        """Record the thread the render pass is running on."""
        self.render_threads.add(threading.current_thread())
        return super()._sample_tile(x, y)

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

    The check is the identity of the thread that runs the pass, taken inside
    the pass itself. The previous form asserted
    `node._wake.is_set() or node.uploads is not None`, and `uploads` is a list
    built in the constructor -- it could not fail, so a `_wake_renderer` that
    rendered inline would have gone unnoticed.
    """
    node = _Threaded()
    caller = threading.current_thread()
    try:
        node._dirty.add((10, 20))
        node._wake_renderer()
        for _ in range(200):
            if node.uploads:
                break
            threading.Event().wait(0.01)
        assert node.uploads, 'the worker never rendered'
        assert node.render_threads == {node._worker}, (
            'the pass ran on {} -- rendering on the executor thread blocks '
            'it for the whole pass'.format(node.render_threads))
        assert caller not in node.render_threads
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


def test_the_manifest_stamp_is_wall_clock():
    """Liveness must not be measured on the ROS clock.

    The page computes age as `Date.now()/1000 - stamp`. Under `use_sim_time`
    -- the documented simulator workflow -- the ROS clock starts near zero,
    so a ROS stamp made the page report a live renderer as permanently stale:
    the one thing the manifest exists to get right.
    """
    node = _Pass()
    node.sim_clock_seconds = 12.5          # a freshly started sim clock
    node._dirty.add((10, 20))
    node._render_dirty()
    meta = node.meta[-1]
    assert abs(meta['stamp'] - time.time()) < 60, (
        'the manifest stamp is not wall clock: the page will read a live '
        'renderer as stale forever under use_sim_time')
    assert meta['ros_stamp'] == 12.5


def test_the_manifest_is_a_heartbeat_even_with_nothing_to_render():
    """An idle renderer must still say it is alive."""
    node = _Pass()
    node._render_dirty()
    assert len(node.meta) == 1
    node._render_dirty()
    assert len(node.meta) == 2


def test_a_pass_without_the_chart_datum_publishes_nothing_but_says_so():
    """Colouring from an unreferenced height is wrong and looks plausible.

    The band carries z in the map frame -- ellipsoidal, -36 to -57 m over the
    Piscataqua -- so without the offset every cell saturates the 0-40 m ramp
    and the whole survey paints the deepest colour. The pass must publish no
    tile, keep the work for next time, and say what it is waiting for: a
    silent no-op reads as "nothing new to render".
    """
    node = _Pass()
    node.datum_available = False
    node._dirty.add((10, 20))
    node._render_dirty()

    assert not node.uploads, 'a tile was coloured without a chart-datum offset'
    assert node._dirty == {(10, 20)}, (
        'the dirty set was consumed by a pass that rendered nothing')
    assert node._rendered == 0
    assert [m['status'] for m in node.meta] == ['waiting_for_chart_datum'], (
        'the page cannot tell a waiting renderer from an idle one')

    # And the held work is rendered as soon as the offset arrives.
    node.datum_available = True
    node._render_dirty()
    assert [key for key, _ in node.uploads] == ['live/coverage/15/10/20.png']
    assert node.meta[-1]['status'] == 'ok'


def test_the_shutdown_flush_is_bounded():
    """The carefully bounded 45 s join must not be followed by an open pass.

    A flush is one 30 s-capped upload per dirty tile. Against a large mosaic
    and a failing endpoint that is hours, with the operator's Ctrl-C already
    spent. Past the deadline the remainder goes back in the dirty set rather
    than being rendered.
    """
    node = _Pass()
    node._dirty.update((x, 0) for x in range(5))
    node._render_dirty(deadline=time.monotonic() - 1.0)
    assert not node.uploads, 'a pass past its deadline still uploaded'
    assert len(node._dirty) == 5, 'the unrendered tiles were dropped'
    assert node.meta, 'the manifest is still published for the pass'

    # A deadline that has not passed does not truncate anything.
    node._render_dirty(deadline=time.monotonic() + 60.0)
    assert len(node.uploads) == 5
    assert not node._dirty


def test_a_second_interrupt_during_shutdown_does_not_skip_cleanup():
    """`stop()` is called from `main()`'s `finally`.

    An impatient second Ctrl-C landing in the join or the final flush used to
    propagate out of `stop()`, skipping `destroy_node()` and
    `rclpy.shutdown()` -- the cleanup the method exists to make orderly.
    """
    node = _Threaded()
    node._worker.join(timeout=0.1)          # nothing to render yet

    def _interrupt(*args, **kwargs):
        raise KeyboardInterrupt()

    node._dirty.add((10, 20))
    node._render_dirty = _interrupt
    node.stop()                              # must not raise
    assert any('interrupted while stopping' in line
               for line in node._logger.lines), (
        'the interrupt was swallowed without a word')


def test_a_frozen_chart_datum_is_reported_rather_than_rendered_as_normal():
    """The offset is the tide. A stale one colours against an old water level.

    After the first successful lookup a total TF outage is invisible: the node
    goes on rendering from the last value it saw and the manifest used to say
    `ok`. Frozen tide reads on the page as ordinary bathymetry.
    """
    node = _Pass()
    node._dirty.add((10, 20))
    node._render_dirty()
    assert node.meta[-1]['status'] == 'ok'
    assert node.meta[-1]['chart_datum_age'] < 1.0

    node._datum_stamp = time.monotonic() - (
        coverage_renderer.DATUM_STALE_SECONDS + 5.0)
    node._dirty.add((10, 21))
    node._render_dirty()
    assert node.meta[-1]['status'] == 'stale_chart_datum', (
        'a frozen chart-datum offset was reported as a healthy render')
    assert node.meta[-1]['chart_datum_age'] > (
        coverage_renderer.DATUM_STALE_SECONDS)
    # Coverage still renders: stale is a degradation, not a stop.
    assert ('live/coverage/15/10/21.png'
            in [key for key, _ in node.uploads])


def test_a_disabled_chart_datum_correction_is_not_stale():
    """An empty chart_datum_frame is the documented "already referenced" case.

    There is no TF to go stale, so the manifest must not start crying wolf
    about an offset nobody publishes.
    """
    node = _Pass()
    node._tf_buffer = None
    node._datum_stamp = None
    node._dirty.add((10, 20))
    node._render_dirty()
    assert node.meta[-1]['status'] == 'ok'
    assert node.meta[-1]['chart_datum_age'] is None


def test_the_failure_counter_survives_concurrent_threads():
    """`_failures` is incremented from the executor and the render thread.

    `+=` on an int is a read-modify-write, so concurrent increments lose
    counts -- and this counter is the whole of what the node reports about
    what went wrong when it shuts down.
    """
    node = _Pass()
    workers = [threading.Thread(
        target=lambda: [node._note_failure() for _ in range(2000)])
        for _ in range(4)]
    for worker in workers:
        worker.start()
    for worker in workers:
        worker.join()
    assert node._failures == 8000
