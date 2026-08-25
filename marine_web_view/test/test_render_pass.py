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
from marine_web_view.s3_upload import S3Uploader

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
        self._stop = threading.Event()
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
        self.meta_max_ages = []
        self.tile_max_ages = []
        self.upload_ok = True
        self.band_name = 'depth'
        self.cache_control = 60
        self.meta_cache_control = coverage_renderer.META_MAX_AGE_SECONDS
        self.render_interval = 20.0
        self.covered = covered
        self.raise_on_sample = False
        self.sim_clock_seconds = 1_700_000_000.0
        self.datum_available = True
        # The correction is enabled (a real Buffer would sit here) and the
        # transform it last read is stamped now on the ROS clock, so
        # `_datum_age` reads as fresh by default. The stamp is ROS time of the
        # DATA, not of the lookup -- see `_transform_seconds`.
        self._tf_buffer = object()
        self._datum_stamp = self.sim_clock_seconds

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

    def _publish(self, payload, key, content_type='image/png', max_age=None):
        """Record an upload attempt, keeping the manifest separate.

        `max_age` is recorded because the manifest asks for a shorter one than
        the tiles: it is the liveness signal, and a cache holding it is a
        cache in which the page cannot see that the renderer has moved.
        """
        if key.endswith('meta.json'):
            assert content_type == 'application/json'
            self.meta.append(json.loads(payload))
            self.meta_max_ages.append(max_age)
            return self.upload_ok
        self.uploads.append((key, payload))
        self.tile_max_ages.append(max_age)
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


def test_the_manifest_carries_a_change_signal_for_the_page():
    """An idle pass must be distinguishable from one that rendered.

    `stamp` moves on every pass, idle or not, so a page that refreshes its
    tile layer on a new stamp tears down and re-requests every tile under
    every viewer's viewport once per `render_interval` in perpetuity -- with
    the sonar off and the boat docked, on a public page, billed to us.
    `rendered_tiles` is the field that says whether anything actually
    changed.

    `published_tiles` cannot do this job, which is why it is asserted against
    here: it is the SIZE of the published set, so it does not move when an
    already-published tile is re-rendered -- exactly what happens as a survey
    line grows inside a tile that is already on the map.
    """
    node = _Pass()
    node._dirty.add((10, 20))
    node._render_dirty()
    first = node.meta[-1]
    assert first['rendered_tiles'] == 1, (
        'the manifest carries no usable count of tiles actually published')

    node._render_dirty()                     # idle: nothing is dirty
    idle = node.meta[-1]
    assert idle['stamp'] >= first['stamp'], 'the stamp went backwards'
    assert idle['rendered_tiles'] == first['rendered_tiles'], (
        'an idle pass moved the change signal: every viewer would tear down '
        'and re-request every visible tile every render_interval forever')

    node._dirty.add((10, 20))                # the same tile, more coverage
    node._render_dirty()
    again = node.meta[-1]
    assert again['published_tiles'] == idle['published_tiles'], (
        'the fixture no longer re-renders an already-published tile, so this '
        'test no longer distinguishes the two candidate signals')
    assert again['rendered_tiles'] != idle['rendered_tiles'], (
        're-rendering an already-published tile did not move the change '
        'signal, so coverage growing inside a tile never reaches the page')


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
    """A flush past its deadline issues no requests at all.

    A flush is one upload per dirty tile and a single upload has no exact
    ceiling (see `_boto3_client`), so against a large mosaic and a wedged
    endpoint an unbounded flush is hours -- with the operator's Ctrl-C
    already spent. Past the deadline the remainder goes back in the dirty
    set, and nothing is published at all -- not even the manifest, because a
    pass that rendered no tile has nothing to announce, whether it aborted
    at the top or ran out of budget before the first upload. (A pass
    truncated part way through DOES publish its manifest; see
    `test_a_stop_part_way_through_a_pass_returns_the_rest`.)
    """
    node = _Pass()
    node._dirty.update((x, 0) for x in range(5))
    node._render_dirty(lambda: True)
    assert not node.uploads, 'a pass past its deadline still uploaded'
    assert not node.meta, 'the manifest PUT ran outside the flush budget'
    assert len(node._dirty) == 5, 'the unrendered tiles were dropped'

    # A deadline that has not passed does not truncate anything.
    deadline = time.monotonic() + 60.0
    node._render_dirty(lambda: time.monotonic() >= deadline)
    assert len(node.uploads) == 5
    assert not node._dirty
    assert node.meta, 'a completed pass still publishes its manifest'


def test_a_scheduled_pass_stops_on_the_stop_event():
    """The bound every scheduled pass used to lack.

    `_render_pending` honoured a deadline only when one was passed, and a
    scheduled pass passes none -- so a pass was as long as the endpoint made
    it, and `stop()`'s join could not win. The check is now on every path,
    and for a scheduled pass the predicate is the stop event.
    """
    node = _Pass()
    node._dirty.update((x, 0) for x in range(5))
    node._stop.set()
    node._render_dirty()                 # the scheduled-pass default
    assert not node.uploads, 'a stopped pass kept uploading tiles'
    assert not node.meta, 'a stopped pass still published a manifest'
    assert len(node._dirty) == 5, 'the unrendered tiles were dropped'


def test_a_stop_part_way_through_a_pass_returns_the_rest():
    """Mid-pass: the tiles already rendered stand, the rest stay dirty."""
    node = _Pass()
    node._dirty.update((x, 0) for x in range(5))
    rendered = []
    original = node._publish

    def _publish(payload, key, **kwargs):
        rendered.append(key)
        if len(rendered) == 2:
            node._stop.set()             # a Ctrl-C lands here
        return original(payload, key, **kwargs)

    node._publish = _publish
    node._render_dirty()
    assert len(node.uploads) == 2, node.uploads
    assert len(node._dirty) == 3, node._dirty
    assert any('stopped with 3 tile(s) left' in line
               for line in node._logger.lines), node._logger.lines
    # And the manifest IS published, saying what happened. The page refreshes
    # its coverage layer only when `rendered_tiles` moves, so without this PUT
    # the two tiles that did land are in the bucket and invisible -- and on
    # the shutdown flush, which is where a truncated pass is likeliest, there
    # is no later pass to announce them.
    assert node.meta, (
        'tiles were published and never announced; the page refreshes on '
        'rendered_tiles, so a viewer never requests them')
    assert node.meta[-1]['status'] == 'truncated_render', (
        'a truncated pass reported as a completed render')
    assert node.meta[-1]['rendered_tiles'] == 2, node.meta[-1]


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

    # The publisher dies: the same transform keeps resolving, so its stamp
    # stays where it was while the ROS clock moves on.
    node.sim_clock_seconds += coverage_renderer.DATUM_STALE_SECONDS + 5.0
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


def test_the_manifest_is_cached_less_than_the_tiles_it_advertises():
    """The liveness signal must not be held as long as the payload.

    `meta.json` is the heartbeat and the change signal, a few hundred bytes
    read once per poll. Stamped with the tiles' own `max-age` -- which is
    matched to `render_interval`, which is also the page's poll period -- a
    cache between the renderer and the viewer can answer a poll with the
    PREVIOUS pass's manifest, and then `rendered_tiles` does not move (so
    newly surveyed ground never appears for a stationary viewer) and `stamp`
    is old (so the age is under-reported and a dead renderer reads alive).
    """
    node = _Pass()
    node._dirty.add((10, 20))
    node._render_dirty()
    assert node.meta_max_ages, 'no manifest was published'
    assert node.tile_max_ages, 'no tile was published'
    manifest = node.meta_max_ages[-1]
    assert manifest is not None, (
        'the manifest is published with no max-age of its own, so it '
        "inherits the tiles' -- the whole defect this pins")
    assert manifest >= 1, (
        'max-age={} is not a valid max-age'.format(manifest))
    assert manifest < node.cache_control, (
        'the manifest is cached for {} s against tiles at {} s: the liveness '
        'signal must be the fresher of the two'
        .format(manifest, node.cache_control))
    # The tiles keep the operator-set value: this must not have become a
    # blanket shortening of every object's max-age, which would multiply the
    # tile request volume the change-signal gate exists to bound.
    assert node.tile_max_ages[-1] in (None, node.cache_control), (
        'tiles are published with max-age={} rather than the configured {}'
        .format(node.tile_max_ages[-1], node.cache_control))


def test_the_manifest_max_age_never_exceeds_the_tiles():
    """`meta_max_age` is a ceiling, not a fixed value.

    An operator who shortens `cache_control` is asking for a fresher display,
    not for the manifest alone to lag behind the tiles it advertises.
    """
    assert coverage_renderer.meta_max_age(60) < 60
    assert coverage_renderer.meta_max_age(3) <= 3
    assert coverage_renderer.meta_max_age(1) == 1
    assert coverage_renderer.meta_max_age(0) >= 1, (
        'max-age=0 is not a valid max-age'
    )


class _RecordingClient:
    """Stand in for a boto3 S3 client, recording every put_object kwarg."""

    def __init__(self, error=None):
        self.calls = []
        self.error = error

    def put_object(self, **kwargs):
        self.calls.append(kwargs)
        if self.error is not None:
            raise self.error
        return {}


def test_the_upload_stamps_the_whole_object_shape_it_is_given():
    """Everything `_publish` is told has to reach the S3 call to mean anything.

    `_publish` is the only place the bucket, key, body, content type and cache
    policy are assembled, so an argument the PUT drops is a comment, not a
    policy. The max-age halves are the load-bearing pair -- the manifest's
    whole job is to be current and the tiles' is to be cached -- but asserting
    only that one field would leave the rest of the call unbound.
    """
    client = _RecordingClient()

    class _Node:
        dry_run = False
        bucket = 'unh-ccom-p11-live'
        profile = ''
        cache_control = 60
        _uploader = S3Uploader('unh-ccom-p11-live', client=client)

        def _note_failure(self):
            raise AssertionError('the upload should not have failed')

        def get_logger(self):
            return _Logger()

    assert CoverageRenderer._publish(
        _Node(), b'{}', 'live/coverage/meta.json',
        content_type='application/json', max_age=5)
    assert CoverageRenderer._publish(
        _Node(), b'png', 'live/coverage/1/2/3.png')

    assert len(client.calls) == 2
    manifest, tile = client.calls
    assert manifest == {
        'Bucket': 'unh-ccom-p11-live',
        'Key': 'live/coverage/meta.json',
        'Body': b'{}',
        'ContentType': 'application/json',
        'CacheControl': 'max-age=5',
    }, 'the manifest was uploaded as {}'.format(manifest)
    assert tile == {
        'Bucket': 'unh-ccom-p11-live',
        'Key': 'live/coverage/1/2/3.png',
        'Body': b'png',
        'ContentType': 'image/png',
        'CacheControl': 'max-age=60',
    }, 'the tile was uploaded as {}'.format(tile)


def test_a_failed_upload_is_counted_and_not_raised():
    """An exception out of `_publish` stops the render thread for good.

    The render worker has no exception handling around its publishes, so a
    transport error has to come back as False-plus-a-counted-failure the way
    the CLI's nonzero exit code did, not as a raise.
    """
    client = _RecordingClient(error=RuntimeError('AccessDenied'))
    counted = []

    class _Node:
        dry_run = False
        bucket = 'unh-ccom-p11-live'
        profile = ''
        cache_control = 60
        _uploader = S3Uploader('unh-ccom-p11-live', client=client)

        def _note_failure(self):
            counted.append(1)

        def get_logger(self):
            return _Logger()

    assert CoverageRenderer._publish(
        _Node(), b'png', 'live/coverage/1/2/3.png') is False
    assert counted == [1], (
        'a failed upload must increment the failure counter, got {}'.format(
            counted))


def test_stop_does_not_wait_out_a_wedged_upload(monkeypatch):
    """Shutdown is bounded by the join, not by the endpoint answering.

    The join used to be 45 s, chosen to be "longer than one upload" against
    a per-PUT ceiling that does not exist -- so two stalled tiles blew it and
    took the early return that skips the final flush. The worker now checks
    the stop event between tiles, so the join only has to cover the request
    it is already inside; losing that race costs a warning and a skipped
    flush, never a hang, because the worker is a daemon thread.
    """
    monkeypatch.setattr(coverage_renderer, 'WORKER_JOIN_SECONDS', 0.3)
    node = _Threaded()
    released = threading.Event()
    entered = threading.Event()
    original = node._publish

    def _wedged(payload, key, **kwargs):
        entered.set()
        released.wait(10.0)
        return original(payload, key, **kwargs)

    node._publish = _wedged
    node._dirty.add((10, 20))
    node._wake_renderer()
    try:
        assert entered.wait(5.0), 'the worker never started an upload'
        start = time.monotonic()
        node.stop()
        elapsed = time.monotonic() - start
        assert elapsed < 3.0, (
            'stop() took {:.1f} s with an upload wedged'.format(elapsed))
        assert any('still inside a request' in line
                   for line in node._logger.lines), node._logger.lines
    finally:
        released.set()


def test_the_shutdown_budget_is_small_enough_to_be_a_shutdown():
    """Both halves of the bound are shipped values, so pin them.

    Worst case is WORKER_JOIN_SECONDS + SHUTDOWN_FLUSH_SECONDS plus the one
    request already in flight. An operator's Ctrl-C has to mean something.
    """
    assert coverage_renderer.WORKER_JOIN_SECONDS == 10.0
    assert coverage_renderer.SHUTDOWN_FLUSH_SECONDS == 30.0


def test_an_abort_before_the_first_tile_announces_nothing():
    """`truncated_render` over a pass that PUT nothing is a false report.

    The abort check at the top of `_render_dirty` is not the only way a pass
    can render nothing: the budget can go in `_update_datum_offset()`, or run
    out on `_render_pending`'s first check. Both used to publish
    `truncated_render` with the PREVIOUS pass's counts -- replacing an
    accurate manifest with a worse one, and, on the page, retiring a live
    readout in favour of a status word about a pass that did nothing.
    """
    node = _Pass()
    node._dirty.update((x, 0) for x in range(3))

    # Aborts only once _render_pending asks, i.e. after the datum update and
    # before the first tile -- the case the top-of-function check misses.
    calls = []

    def _abort():
        calls.append(True)
        return len(calls) > 1

    node._render_dirty(_abort)
    assert not node.uploads, 'a pass past its budget still uploaded a tile'
    assert not node.meta, (
        'a pass that rendered nothing published a truncated_render manifest')
    assert len(node._dirty) == 3, 'the unrendered tiles were dropped'

    # A pass that DID publish before aborting still announces, unchanged.
    node = _Pass()
    node._dirty.update((x, 0) for x in range(3))
    calls = []

    def _abort_after_one():
        calls.append(True)
        return len(calls) > 2

    node._render_dirty(_abort_after_one)
    assert len(node.uploads) == 1, node.uploads
    assert node.meta and node.meta[-1]['status'] == 'truncated_render', (
        'tiles were PUT and never announced')
