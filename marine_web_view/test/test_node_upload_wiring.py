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

"""Bind the UPLOAD WIRING in both nodes, not just the helpers it calls.

A review round found seven mutations surviving a green suite, every one of
them at a call site: the numbers each node hands the transport, and which
transport it hands them to. The helpers were bound and the wiring was bare,
so the arguments the round's conclusions rested on could all be changed
without a test noticing.

This module runs the real constructors with a recording stand-in for
`S3Uploader`, which is the only way those lines execute. Discovery is turned
off and the domain moved aside so a test run cannot reach a renderer running
on the same host (the same treatment, and for the same reason, as
test_dry_run_needs_no_aws).
"""

import os

os.environ['ROS_DOMAIN_ID'] = '101'
os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'OFF'

import threading                                        # noqa: E402
import time                                             # noqa: E402

from marine_web_view import coverage_renderer           # noqa: E402
from marine_web_view import state_renderer              # noqa: E402
from marine_web_view.s3_upload import AsyncUploader     # noqa: E402

import pytest                                           # noqa: E402

import rclpy                                            # noqa: E402


class _Recorder:
    """Stand-in for S3Uploader that records how it was constructed."""

    instances = []

    def __init__(self, bucket, profile=None, connect_timeout=5,
                 read_timeout=None, **kwargs):
        self.bucket = bucket
        self.profile = profile
        self.connect_timeout = connect_timeout
        self.read_timeout = read_timeout
        self.calls = []
        _Recorder.instances.append(self)

    def put(self, payload, key, content_type, cache_control):
        """Record one upload and succeed."""
        self.calls.append((key, payload, content_type, cache_control))
        return True, None


class _Stalling(_Recorder):
    """A recorder whose PUTs never return until released."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.entered = threading.Semaphore(0)
        self.release = threading.Event()

    def put(self, payload, key, content_type, cache_control):
        """Block until released, then record."""
        self.entered.release()
        self.release.wait(10.0)
        return super().put(payload, key, content_type, cache_control)


@pytest.fixture
def live_node(monkeypatch, request):
    """Start rclpy with dry_run OFF and the transport replaced."""
    uploader = getattr(request, 'param', _Recorder)
    _Recorder.instances = []
    monkeypatch.setattr(coverage_renderer, 'S3Uploader', uploader)
    monkeypatch.setattr(state_renderer, 'S3Uploader', uploader)
    rclpy.init(args=[
        '--ros-args',
        '-p', 'dry_run:=false',
        '-p', 'bucket:=unh-ccom-p11-live',
        '-p', 'profile:=p11-renderer',
    ])
    try:
        yield _Recorder.instances
    finally:
        rclpy.shutdown()


def test_state_renderer_uploads_through_the_worker_not_the_executor(
        live_node):
    """The node must hold an AsyncUploader, not a bare transport.

    Wiring the transport straight back in is the mutation this exists to
    fail: it compiles, every helper test still passes, and the only symptom
    is a nav-fix callback that stops running whenever S3 is slow.
    """
    node = state_renderer.StateRenderer()
    try:
        assert isinstance(node._sender, AsyncUploader), (
            'state_renderer no longer publishes through the upload worker')
        assert live_node[0].read_timeout == state_renderer.UPLOAD_READ_TIMEOUT
        assert live_node[0].read_timeout == 15, (
            'the worker read timeout changed; it is the only thing stopping '
            'one dead connection from occupying the worker forever')
        assert live_node[0].profile == 'p11-renderer', (
            'the profile no longer reaches the transport uncoalesced')
    finally:
        node.stop()
        node.destroy_node()


def test_coverage_renderer_passes_its_own_read_timeout(live_node):
    """The other call site, and the one that coalesces its profile."""
    node = coverage_renderer.CoverageRenderer()
    try:
        assert live_node[0].read_timeout == (
            coverage_renderer.UPLOAD_READ_TIMEOUT)
        assert live_node[0].read_timeout == 25
        assert live_node[0].profile == 'p11-renderer'
    finally:
        node.stop()
        node.destroy_node()


@pytest.mark.parametrize('live_node', [_Stalling], indirect=True)
def test_a_stalled_upload_does_not_stop_the_position_timer(live_node):
    """The finding this restructure exists for, at the node level.

    `_tick` and `_on_fix` are both callbacks on rclpy.spin's
    single-threaded executor. With the PUT on that thread, a stalled upload
    stops fixes being recorded for its whole duration -- and they are not
    replayed, so the track keeps a permanent hole. Here the transport never
    answers; the timer callback must still return, and the subscription must
    still be able to record a fix behind it.
    """
    node = state_renderer.StateRenderer()
    transport = live_node[0]
    try:
        node._fix = _fix(1000.0)
        node._history.append((1000.0, 43.0, -70.0))
        node._tick()
        assert transport.entered.acquire(timeout=5.0), (
            'the worker never started the upload')

        # The transport is now wedged. Ten more ticks and a fix behind it.
        start = time.monotonic()
        for index in range(1, 11):
            node._fix = _fix(1000.0 + index)
            node._tick()
        node._on_fix(_fix(1011.0))
        elapsed = time.monotonic() - start
        assert elapsed < 1.0, (
            'ten ticks behind a stalled upload took {:.1f} s; the executor '
            'thread is waiting on the network'.format(elapsed))
        assert node._history[-1][0] == 1011.0, (
            'the fix behind the stalled upload was never recorded')
        assert node._sender.counts()[0] == 0, 'nothing should have completed'
    finally:
        transport.release.set()
        node.stop()
        node.destroy_node()


def _fix(stamp):
    """Return a NavSatFix stamped at `stamp` seconds."""
    from sensor_msgs.msg import NavSatFix
    message = NavSatFix()
    message.header.stamp.sec = int(stamp)
    message.header.stamp.nanosec = int((stamp - int(stamp)) * 1e9)
    message.header.frame_id = 'base_link'
    message.latitude = 43.13
    message.longitude = -70.94
    message.altitude = 1.0
    return message


def test_an_unconfirmed_upload_is_offered_again_on_the_next_tick(live_node):
    """Acceptance is not publication.

    The old `_put` returned True only when the object was on the wire, and
    `_last_sent_stamp` moved only then, so a failed upload was retried a tick
    later. That contract has to survive the move off-thread: the change
    detection compares against what the worker CONFIRMED, not against what it
    accepted.
    """
    node = state_renderer.StateRenderer()
    try:
        sent = []

        class _Failing:
            """A worker stand-in that never confirms anything."""

            def submit(self, payload, key, content_type, cache, tag=None):
                """Accept everything, confirm nothing."""
                sent.append((key, tag))
                return True

            def confirmed(self, key):
                """Report that nothing has ever landed."""
                return None

            def stop(self, timeout=None):
                """Stop cleanly, like the real worker."""
                return True

        node._sender = _Failing()
        node._fix = _fix(1000.0)
        node._tick()
        node._tick()
        assert sent == [('live/position.geojson', 1000.0)] * 2, sent
        assert node._skipped == 0, (
            'an unpublished position was counted as unchanged')
    finally:
        node.stop()
        node.destroy_node()
