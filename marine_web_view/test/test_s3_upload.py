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

"""Pin the S3 upload seam: what reaches the PUT, and what comes back.

This module is the single place both renderers' bytes leave the process, so a
dropped keyword is a silently wrong cache policy or a survey published under
the wrong key, and an exception that escapes instead of coming back as a value
kills the timer or the render thread that called it.

Every test here injects a stub client, so nothing in this file needs the AWS
SDK installed -- which is also the property that keeps a dry-run host free of
it (see the module docstring of marine_web_view/s3_upload.py).
"""

from marine_web_view import s3_upload
from marine_web_view.s3_upload import AsyncUploader
from marine_web_view.s3_upload import describe_error
from marine_web_view.s3_upload import S3Uploader

import pytest


def _settle(predicate, timeout=5.0):
    """Poll `predicate` until true or `timeout` expires; return the result.

    The upload worker is a real thread, so every assertion about what reached
    the transport is inherently a wait. A generous timeout with an early exit
    keeps that from being either flaky or slow.
    """
    import time
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.005)
    return bool(predicate())


class _Client:
    """Record put_object kwargs; optionally raise a scripted error."""

    def __init__(self, error=None):
        self.calls = []
        self.error = error

    def put_object(self, **kwargs):
        self.calls.append(kwargs)
        if self.error is not None:
            raise self.error
        return {'ETag': '"d41d8cd98f00b204e9800998ecf8427e"'}


class _ClientError(Exception):
    """Shaped like botocore's ClientError, without needing botocore."""

    def __init__(self, code, message):
        super().__init__('{}: {}'.format(code, message))
        self.response = {'Error': {'Code': code, 'Message': message}}


def test_a_successful_put_passes_every_field_through_unchanged():
    """The five fields are the whole contract; a dropped one is silent."""
    client = _Client()
    uploader = S3Uploader('unh-ccom-p11-live', client=client)

    assert uploader.put(b'payload', 'live/position.geojson',
                        'application/geo+json', 'max-age=1') == (True, None)
    assert client.calls == [{
        'Bucket': 'unh-ccom-p11-live',
        'Key': 'live/position.geojson',
        'Body': b'payload',
        'ContentType': 'application/geo+json',
        'CacheControl': 'max-age=1',
    }]


@pytest.mark.parametrize('code', ['AccessDenied', 'NoSuchBucket',
                                  'SlowDown', 'ThrottlingException'])
def test_a_client_error_comes_back_as_a_value_not_a_raise(code):
    """Both callers count failures; neither catches, so a raise strands them.

    state_renderer's timer and coverage_renderer's render worker both call
    straight into `put` with no try around it.
    """
    error = _ClientError(code, 'denied or throttled')
    uploader = S3Uploader('unh-ccom-p11-live', client=_Client(error=error))

    ok, exc = uploader.put(b'x', 'live/k', 'image/png', 'max-age=20')
    assert ok is False
    assert exc is error
    assert code in describe_error(exc), (
        'the S3 error code is the one thing an operator acts on; '
        'describe_error gave {!r}'.format(describe_error(exc)))


def test_a_transport_error_without_a_response_still_describes_itself():
    """Timeouts and connection resets carry no Error dict at all."""
    described = describe_error(OSError('connection reset by peer'))
    assert 'OSError' in described and 'connection reset' in described


def test_only_the_declared_error_classes_are_caught():
    """A programming error must not be laundered into a counted upload failure.

    An injected client defaults to catching Exception (the test seam), so this
    pins the real path: when the caught classes are declared, anything else
    propagates instead of being reported as a transport failure the node would
    retry forever.
    """
    uploader = S3Uploader('b', client=_Client(error=TypeError('bad body')),
                          transport_errors=(_ClientError,))
    with pytest.raises(TypeError):
        uploader.put(b'x', 'k', 'image/png', 'max-age=1')


def test_the_profile_reaches_the_client_factory_exactly_as_given():
    """Coalescing here would erase the two nodes' deliberate asymmetry.

    The distinction lives in the two constructor call sites, not in any
    flag: coverage_renderer passes `profile or None` so an unset profile
    means the default credential chain (an EC2 instance role), while
    state_renderer passes its profile through so a blanked one fails loudly.
    Both depend on this helper not second-guessing the value it is handed.
    """
    seen = []
    real = s3_upload._boto3_client

    def _fake(profile, connect_timeout, read_timeout):
        seen.append((profile, connect_timeout, read_timeout))
        return _Client(), (_ClientError,)

    s3_upload._boto3_client = _fake
    try:
        for profile in (None, '', 'p11-renderer'):
            S3Uploader('unh-ccom-p11-live', profile=profile, read_timeout=15)
    finally:
        s3_upload._boto3_client = real

    assert [entry[0] for entry in seen] == [None, '', 'p11-renderer'], (
        'the profile was rewritten on the way to boto3: {}'.format(seen))
    assert [entry[2] for entry in seen] == [15, 15, 15], (
        'the read timeout each node sets must reach the client config: '
        '{}'.format(seen))


def test_the_position_upload_carries_geojson_and_the_interval_max_age():
    """state_renderer's own cache policy, pinned through the upload worker.

    The position object's max-age IS the publish interval: hold it longer and
    a viewer sits on a stale fix; the object is typed application/geo+json so
    the page's fetch parses it as GeoJSON. Neither survives a `_queue` that
    drops what it is handed, and the payload now crosses a thread on the way
    out, so this runs the real worker rather than the transport alone.
    """
    from marine_web_view.state_renderer import StateRenderer

    client = _Client()
    sender = AsyncUploader(S3Uploader('unh-ccom-p11-live', client=client))

    class _Node:
        bucket = 'unh-ccom-p11-live'
        _sender = sender

        def get_logger(self):
            raise AssertionError('the upload should not have failed')

    node = _Node()
    assert StateRenderer._queue(node, '{"type": "Feature"}',
                                'live/position.geojson', 1.0, 12.5) is True
    assert _settle(lambda: client.calls), 'nothing reached the transport'
    assert client.calls == [{
        'Bucket': 'unh-ccom-p11-live',
        'Key': 'live/position.geojson',
        'Body': b'{"type": "Feature"}',
        'ContentType': 'application/geo+json',
        'CacheControl': 'max-age=1',
    }]
    assert sender.confirmed('live/position.geojson') == 12.5
    assert sender.stop(timeout=2.0)


def test_a_failed_position_upload_is_counted_not_raised():
    """The upload runs on a ROS timer; a raise there stops publishing."""
    logged = []

    class _Logger:
        """Capture the error line the node logs."""

        def error(self, message, **kwargs):
            logged.append(message)

    from marine_web_view.state_renderer import StateRenderer

    class _Node:
        """A node whose failure logger is the real one, called off-thread."""

        def get_logger(self):
            """Return the capturing logger."""
            return _Logger()

        _log_upload_failure = StateRenderer._log_upload_failure

    node = _Node()
    sender = AsyncUploader(
        S3Uploader('unh-ccom-p11-live',
                   client=_Client(error=_ClientError('AccessDenied', 'nope'))),
        log_error=node._log_upload_failure)
    node._sender = sender
    node.bucket = 'unh-ccom-p11-live'

    # Accepted, because acceptance is not publication: the worker discovers
    # the failure, and `confirmed` keeps reporting the older stamp so the
    # next tick offers this object again.
    assert StateRenderer._queue(node, '{}', 'live/position.geojson',
                                1.0, 7.0) is True
    assert _settle(lambda: sender.counts()[1] == 1), sender.counts()
    assert sender.confirmed('live/position.geojson') is None
    assert logged and 'AccessDenied' in logged[0], logged
    assert sender.stop(timeout=2.0)


class _FakeConfig:
    """Stand-in for botocore.config.Config that just records its kwargs."""

    def __init__(self, **kwargs):
        self.kwargs = kwargs


class _FakeSession:
    """Stand-in for boto3.Session that records the profile it was given."""

    def __init__(self, profile_name=None):
        self.profile_name = profile_name
        self.config = None

    def client(self, service, config=None):
        """Record the service and config, return a recording stub client."""
        assert service == 's3'
        self.config = config
        return _Client()


def _install_fake_sdk(monkeypatch):
    """Put stand-in boto3/botocore modules in sys.modules; return the log.

    This is how the REAL `_boto3_client` body gets executed without the AWS
    SDK: monkeypatch restores sys.modules afterwards, so
    `test_importing_the_module_does_not_import_boto3` still sees a boto3-free
    process. Without this the client Config -- the whole retry/timeout budget
    -- is unbound: nothing else in the suite ever runs those five lines.
    """
    import sys
    import types

    sessions = []

    def _session(profile_name=None):
        session = _FakeSession(profile_name=profile_name)
        sessions.append(session)
        return session

    boto3_mod = types.ModuleType('boto3')
    boto3_mod.Session = _session
    botocore = types.ModuleType('botocore')
    config_mod = types.ModuleType('botocore.config')
    config_mod.Config = _FakeConfig
    exceptions_mod = types.ModuleType('botocore.exceptions')
    exceptions_mod.ClientError = _ClientError
    exceptions_mod.BotoCoreError = OSError
    monkeypatch.setitem(sys.modules, 'boto3', boto3_mod)
    monkeypatch.setitem(sys.modules, 'botocore', botocore)
    monkeypatch.setitem(sys.modules, 'botocore.config', config_mod)
    monkeypatch.setitem(sys.modules, 'botocore.exceptions', exceptions_mod)
    return sessions


def test_the_client_config_is_what_reaches_botocore(monkeypatch):
    """Every field of the Config is load-bearing; pin all of them.

    Deliberately NOT expressed as a per-PUT wall-clock ceiling. Two rounds of
    this change asserted one -- `attempts * (connect + read)` -- and both were
    wrong: urllib3 applies `connect_timeout` per address returned by
    `getaddrinfo` and the S3 endpoint resolves to several A records, so the
    arithmetic understated the worst case by ~2x on top of the 4x the retry
    count had already added. Nothing in either node is built on that number
    now (see `AsyncUploader` and `coverage_renderer._render_dirty`), so what
    is worth pinning is what actually reaches botocore.
    """
    sessions = _install_fake_sdk(monkeypatch)
    s3_upload._boto3_client('p11-renderer', 5, 15)

    assert len(sessions) == 1
    assert sessions[0].config.kwargs == {
        'connect_timeout': 5,
        'read_timeout': 15,
        'retries': {'mode': 'standard', 'max_attempts': 1},
    }, sessions[0].config.kwargs


def test_the_profile_reaches_boto3_session_uncoalesced(monkeypatch):
    """`profile or None` here would erase the two nodes' asymmetry.

    The existing factory test stops at the monkeypatched `_boto3_client`;
    this one runs the real body, so the one word that state_renderer's
    fail-loudly contract rests on is actually bound.
    """
    sessions = _install_fake_sdk(monkeypatch)
    for profile in (None, '', 'p11-renderer'):
        s3_upload._boto3_client(profile, 5, 15)

    assert [session.profile_name for session in sessions] == [
        None, '', 'p11-renderer'], (
        'the profile was rewritten between the helper and boto3.Session: '
        '{}'.format([session.profile_name for session in sessions]))


def test_importing_the_module_does_not_import_boto3():
    """A dry-run host and this test suite must not need the AWS SDK.

    Both renderers import s3_upload unconditionally, so a module-level
    `import boto3` would make the SDK an import-time requirement for exactly
    the configuration documented as needing none -- and would make these
    tests unrunnable on a host without it.
    """
    import sys

    assert 'marine_web_view.s3_upload' in sys.modules
    assert 'boto3' not in sys.modules, (
        's3_upload pulled boto3 in at import time; the lazy import in '
        '_boto3_client is what keeps a dry-run host free of it')


class _Stalling:
    """An uploader whose PUTs block until released, recording each one.

    This is the condition every claim in this section is about: an endpoint
    that accepted the connection and then stopped answering. It is also the
    only way to test the structure rather than the arithmetic -- a stalled
    PUT is exactly what no timeout value was able to bound.
    """

    def __init__(self):
        import threading
        self.entered = threading.Semaphore(0)
        self.release = threading.Event()
        self.sent = []
        self._lock = threading.Lock()

    def put(self, payload, key, content_type, cache_control):
        """Block until released, then record and succeed."""
        self.entered.release()
        self.release.wait(10.0)
        with self._lock:
            self.sent.append((key, payload))
        return True, None


def test_submitting_while_the_transport_is_stalled_does_not_block():
    """The whole point of the worker.

    `_tick` and `_on_fix` share rclpy.spin's single-threaded executor, so a
    PUT issued from a timer callback stops position fixes being recorded for
    as long as it takes -- and they are not replayed, so the track keeps a
    permanent hole for the stall. No `read_timeout` makes that acceptable.
    """
    import time

    transport = _Stalling()
    sender = AsyncUploader(transport)
    try:
        assert sender.submit(b'first', 'live/position.geojson', 'a', 'b',
                             tag=1)
        assert transport.entered.acquire(timeout=5.0), (
            'the worker never started the first PUT')

        # The transport is now inside a PUT that will not return.
        start = time.monotonic()
        for index in range(20):
            assert sender.submit(b'later', 'live/position.geojson', 'a', 'b',
                                 tag=index)
        elapsed = time.monotonic() - start
        assert elapsed < 1.0, (
            'submitting behind a stalled PUT took {:.1f} s -- the caller is '
            'still waiting on the network'.format(elapsed))
    finally:
        transport.release.set()
        sender.stop(timeout=5.0)


def test_a_stalled_worker_drops_superseded_payloads_rather_than_queueing():
    """Latest-wins, and bounded: 20 offers behind a stall cost one send.

    A queue here would grow for as long as the endpoint is slow and then
    publish a march of stale positions, each one already contradicted by the
    next. Both objects this node writes are complete snapshots, so the
    superseded ones carry nothing the newest lacks.
    """
    transport = _Stalling()
    sender = AsyncUploader(transport)
    try:
        sender.submit(b'first', 'live/position.geojson', 'a', 'b', tag=0)
        assert transport.entered.acquire(timeout=5.0)
        for index in range(1, 21):
            sender.submit('n{}'.format(index).encode(),
                          'live/position.geojson', 'a', 'b', tag=index)
        transport.release.set()
        assert _settle(lambda: sender.confirmed(
            'live/position.geojson') == 20), sender.counts()
        # One in flight plus exactly one survivor -- not 21.
        assert [payload for _, payload in transport.sent] == [b'first',
                                                              b'n20'], (
            transport.sent)
        writes, failures, dropped = sender.counts()
        assert (writes, failures, dropped) == (2, 0, 0)
    finally:
        transport.release.set()
        sender.stop(timeout=5.0)


def test_the_pending_map_cannot_grow_past_its_slot_cap():
    """Bounded independently of the caller's key discipline.

    The per-key rule already bounds this node at two keys. The cap is what
    makes the bound a property of the class rather than of its caller: a
    future third artifact cannot turn a stall into unbounded memory.
    """
    transport = _Stalling()
    sender = AsyncUploader(transport, max_slots=2)
    try:
        sender.submit(b'x', 'in/flight', 'a', 'b')
        assert transport.entered.acquire(timeout=5.0)
        assert sender.submit(b'x', 'key/1', 'a', 'b')
        assert sender.submit(b'x', 'key/2', 'a', 'b')
        for index in range(3, 10):
            assert not sender.submit(b'x', 'key/{}'.format(index), 'a', 'b'), (
                'a new key was accepted past the slot cap')
        # Replacing an occupied slot is always allowed -- that is latest-wins.
        assert sender.submit(b'y', 'key/1', 'a', 'b')
        assert sender.counts()[2] == 7, sender.counts()
    finally:
        transport.release.set()
        sender.stop(timeout=5.0)


def test_stop_returns_promptly_even_with_a_put_in_flight():
    """Shutdown is bounded whatever the endpoint does.

    `stop()` cannot cancel a request already in the socket, so what it
    guarantees is that it RETURNS: the worker is a daemon thread, and the
    caller is `main()`'s `finally`, which still has to reach
    `destroy_node()`.
    """
    import time

    transport = _Stalling()
    sender = AsyncUploader(transport)
    try:
        sender.submit(b'x', 'live/position.geojson', 'a', 'b')
        assert transport.entered.acquire(timeout=5.0)
        start = time.monotonic()
        assert sender.stop(timeout=0.5) is False, (
            'stop claimed the worker ended while it was inside a PUT')
        elapsed = time.monotonic() - start
        assert elapsed < 2.0, (
            'stop() took {:.1f} s with a PUT in flight'.format(elapsed))
    finally:
        transport.release.set()


def test_the_worker_does_not_start_new_work_after_stop():
    """A stop must not be followed by another PUT to a dead endpoint."""
    transport = _Stalling()
    transport.release.set()                  # PUTs return immediately
    sender = AsyncUploader(transport)
    sender.submit(b'x', 'first', 'a', 'b')
    assert _settle(lambda: sender.counts()[0] == 1)
    assert sender.stop(timeout=5.0)
    sender.submit(b'x', 'second', 'a', 'b')
    import time
    time.sleep(0.2)
    assert [key for key, _ in transport.sent] == ['first'], transport.sent


def test_a_stop_during_a_drain_does_not_start_the_next_request():
    """The stop check between requests, not just around the wait.

    The case is a stop that arrives while the worker is mid-drain: one PUT
    in the socket and another payload already pending. Finishing the first
    is unavoidable; starting the second would extend a shutdown that the
    caller has already bounded, against an endpoint that has just proved it
    is not answering.
    """
    import time

    transport = _Stalling()
    sender = AsyncUploader(transport)
    try:
        sender.submit(b'a', 'in/flight', 'x', 'y')
        assert transport.entered.acquire(timeout=5.0)
        sender.submit(b'b', 'still/pending', 'x', 'y')

        assert sender.stop(timeout=0.2) is False
        transport.release.set()
        time.sleep(0.5)
        assert [key for key, _ in transport.sent] == ['in/flight'], (
            'the worker started another request after being stopped: '
            '{}'.format(transport.sent))
    finally:
        transport.release.set()


def test_a_busy_key_does_not_starve_the_other_one():
    """The track and the position share one worker, deliberately.

    The position is offered 30x as often as the track. FIFO across keys with
    latest-wins inside one is what keeps the slower object from being
    perpetually overtaken.
    """
    transport = _Stalling()
    sender = AsyncUploader(transport)
    try:
        sender.submit(b'x', 'in/flight', 'a', 'b')
        assert transport.entered.acquire(timeout=5.0)
        sender.submit(b'p1', 'live/position.geojson', 'a', 'b')
        sender.submit(b't1', 'live/track.geojson', 'a', 'b')
        for index in range(2, 30):
            sender.submit('p{}'.format(index).encode(),
                          'live/position.geojson', 'a', 'b')
        transport.release.set()
        assert _settle(lambda: len(transport.sent) == 3), transport.sent
        assert [key for key, _ in transport.sent] == [
            'in/flight', 'live/position.geojson', 'live/track.geojson'], (
            transport.sent)
        assert transport.sent[1][1] == b'p29', 'not the newest position'
    finally:
        transport.release.set()
        sender.stop(timeout=5.0)


class _Raising:
    """A transport whose put RAISES instead of returning (False, exc).

    Exactly what `S3Uploader.put` does when the endpoint fails in a way
    botocore does not model as one of the declared `transport_errors`, and
    what any stub or future transport is free to do.
    """

    def __init__(self, error):
        self.error = error
        self.calls = 0

    def put(self, payload, key, content_type, cache_control):
        self.calls += 1
        raise self.error


def test_a_put_that_raises_costs_the_object_not_the_worker():
    """The silent death: a frozen map that every indicator reports healthy.

    Reproduced by direct execution against the unguarded worker -- the thread
    ended on the first raise, after which `submit` returned True forever,
    `confirmed` never advanced, `counts()` reported 0 failures and `stop()`
    reported a clean shutdown, with nothing in the node's log.
    """
    logged = []
    sender = AsyncUploader(_Raising(RuntimeError('not a transport error')),
                           log_error=lambda key, exc: logged.append(key))
    try:
        assert sender.submit(b'x', 'live/position.geojson', 'a', 'b',
                             tag='one')
        assert _settle(lambda: sender.counts()[1] == 1), sender.counts()
        assert logged == ['live/position.geojson'], (
            'a failed upload has to reach the operator; the worker is the '
            'only thing that saw it')
        assert sender.dead() is None, 'one bad object killed the worker'
        assert sender.confirmed('live/position.geojson') is None

        # And it is still working: the next tick is served, not swallowed.
        assert sender.submit(b'y', 'live/track.geojson', 'a', 'b', tag='two')
        assert _settle(lambda: sender.counts()[1] == 2), sender.counts()
    finally:
        sender.stop(timeout=5.0)


def test_a_log_error_that_raises_does_not_kill_the_worker():
    """`log_error` is the caller's code, called on the worker thread."""
    def _explode(key, exc):
        raise ValueError('the logger itself failed')

    sender = AsyncUploader(_Raising(RuntimeError('transport')),
                           log_error=_explode)
    try:
        sender.submit(b'x', 'first', 'a', 'b')
        assert _settle(lambda: sender.counts()[1] == 1)
        assert sender.dead() is None, 'a broken logger killed the transport'
        sender.submit(b'x', 'second', 'a', 'b')
        assert _settle(lambda: sender.counts()[1] == 2), sender.counts()
    finally:
        sender.stop(timeout=5.0)


class _HostileMap(dict):
    """Fails where no per-send guard can see it: under the worker's lock."""

    def pop(self, key):
        raise MemoryError('out of memory in the worker loop')


def test_a_worker_that_dies_anyway_says_so_on_every_channel():
    """If the thread does end, nothing it publishes is ever sent again.

    So the death must be visible in all four things a caller reads --
    otherwise the map freezes mid-survey while the node reports a clean run.
    """
    sender = AsyncUploader(_Raising(RuntimeError('unused')))
    sender._pending = _HostileMap()
    sender.submit(b'x', 'live/position.geojson', 'a', 'b', tag='one')

    assert _settle(lambda: sender.dead() is not None), (
        'the worker died with nothing recording why')
    assert 'MemoryError' in sender.dead()
    assert sender.counts()[1] == 1, 'the death was not counted as a failure'
    assert sender.submit(b'x', 'k', 'a', 'b') is False, (
        'accepting a payload no thread will ever send')
    assert sender.counts()[2] == 1, 'the refused payload was not counted'
    assert sender.stop(timeout=5.0) is False, (
        'a dead worker reported as a clean shutdown')
