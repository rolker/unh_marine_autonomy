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

"""Shared S3 upload transport for the web-view renderers.

Both renderers write single objects to one bucket, with a per-object
Content-Type and Cache-Control, and both need a failure to come back as a
value they can count rather than an exception that kills a timer or a render
thread. That is the whole of this module.

boto3 is imported LAZILY, inside the client factory, rather than at module
scope. Both renderers import this module unconditionally, but a
``dry_run`` / ``local_dir`` host -- the simulator workflow, and this package's
own test suite -- never constructs an uploader and needs no AWS SDK at all.
A module-level import would make boto3 a hard import-time requirement for
exactly the configuration that is documented as needing none, and would make
the package's tests unrunnable on a host without it.

See rolker/unh_marine_autonomy#351.
"""

import threading


def _boto3_client(profile, connect_timeout, read_timeout):
    """Build an S3 client and the exception classes its calls can raise.

    Returns ``(client, transport_errors)``. The profile is passed through
    EXACTLY as given: each caller decides whether an unset profile means
    ``None`` (fall through to the default credential chain -- an instance
    role, or a plain ~/.aws/credentials) or the empty string (an operator
    mistake that should fail). Coalescing here would erase that distinction.

    ``max_attempts=1`` means ONE attempt, no SDK retry. That is deliberate,
    but it is NOT a wall-clock guarantee and nothing in this package may be
    built on one. ``connect_timeout`` is applied by urllib3 PER ADDRESS
    returned by ``getaddrinfo``, and the S3 endpoint resolves to several A
    records (eight, on the host this was measured on), so the worst case for
    a single ``put`` is that many connect timeouts plus a read timeout -- not
    ``connect_timeout + read_timeout``. Two earlier rounds of this change
    wrote a specific ceiling into comments here and in both callers; both
    were wrong, by 4x and then again by ~2x.

    What the callers rely on instead is structural, and neither depends on
    how long one PUT takes:

    * ``state_renderer`` hands its payloads to an ``AsyncUploader`` (below),
      so a stalled PUT never runs on ``rclpy.spin``'s executor thread.
    * ``coverage_renderer``'s render pass checks an abort predicate between
      tiles, so a stop or a flush deadline takes effect without waiting for
      an upload that is already in flight.

    Losing SDK retry still costs nothing: BOTH nodes retry on their own
    schedule -- state_renderer on the next timer tick, coverage_renderer by
    leaving the tile dirty for the next render pass -- which is the behaviour
    the CLI shell-out this replaces had. A transient failure is counted,
    logged, and retried a tick later instead of held.
    """
    import boto3
    from botocore.config import Config
    from botocore.exceptions import BotoCoreError, ClientError

    config = Config(connect_timeout=connect_timeout,
                    read_timeout=read_timeout,
                    retries={'mode': 'standard', 'max_attempts': 1})
    session = boto3.Session(profile_name=profile)
    return session.client('s3', config=config), (ClientError, BotoCoreError)


def describe_error(exc):
    """Return a short, loggable description of an upload failure.

    A ClientError carries the S3 error code, which is what distinguishes the
    three cases an operator has to act on differently -- AccessDenied (fix
    the policy or profile), NoSuchBucket (fix the parameter), and
    SlowDown/ThrottlingException (nothing to fix, the SDK already backed
    off). Exit-code parsing of CLI stderr could not tell them apart.
    """
    response = getattr(exc, 'response', None)
    if isinstance(response, dict):
        error = response.get('Error') or {}
        code = error.get('Code')
        if code:
            return '{}: {}'.format(code, error.get('Message', ''))[:300]
    return '{}: {}'.format(type(exc).__name__, exc)[:300]


class S3Uploader:
    """Put single objects into one bucket, reporting failures as values."""

    def __init__(self, bucket, profile=None, connect_timeout=5,
                 read_timeout=25, client=None, transport_errors=None):
        """Hold a client for `bucket`, built from `profile` unless injected.

        `client` and `transport_errors` are the test seam: passing a stub
        client means no boto3 import happens at all, which is what lets these
        tests run on a host with no AWS SDK installed. A stub raises whatever
        it likes, so an injected client without an explicit
        `transport_errors` catches Exception.
        """
        self.bucket = bucket
        if client is None:
            client, transport_errors = _boto3_client(
                profile, connect_timeout, read_timeout)
        elif transport_errors is None:
            transport_errors = (Exception,)
        self._client = client
        self._errors = transport_errors

    def put(self, payload, key, content_type, cache_control):
        """Upload `payload` bytes to `key`; return ``(ok, exception_or_None)``.

        Single-part by construction (``put_object``): the objects both
        renderers publish are a GeoJSON point, a short track, a 256x256 PNG
        and a small JSON manifest.
        """
        try:
            self._client.put_object(
                Bucket=self.bucket, Key=key, Body=payload,
                ContentType=content_type, CacheControl=cache_control)
        except self._errors as exc:
            return False, exc
        return True, None


class AsyncUploader:
    """Run PUTs on one background thread, keeping only the newest per key.

    ``state_renderer`` publishes a position every second and a track every
    thirty from ``rclpy.spin``'s SINGLE-THREADED executor. Calling
    ``S3Uploader.put`` there means every second a PUT spends stalled is a
    second the nav-fix subscription does not run -- and those fixes are not
    replayed, so the track keeps a permanent hole for the stall. No timeout
    value fixes that; only getting the network call off that thread does.

    Bounded by construction, in two independent ways:

    * **One slot per key.** ``submit`` REPLACES whatever is pending for the
      same key rather than queueing behind it. Both objects this node
      publishes are complete snapshots of the present -- the position is the
      latest fix, the track is rebuilt from the whole history window -- so a
      superseded payload has no information the next one lacks. Dropping it
      is not data loss; queueing it would be, because the queue would grow
      for as long as the endpoint is slow and then publish a march of stale
      positions.
    * **A hard slot cap.** The keys are a fixed pair, so the per-key rule
      already bounds the map at two. ``max_slots`` makes that independent of
      the caller: a submission for a NEW key when the map is full is refused
      and counted, so no call pattern can grow this unboundedly.

    **One worker, shared by both objects, deliberately.** Per-key slots make
    the only ordering that matters -- within a key -- free, and a single
    worker gives it without a lock per key. A second thread would not buy
    liveness for the position: a stall is a property of the endpoint, not of
    the object being written, so the two would stall together. What it would
    buy is a second shutdown path and a second connection out of a pool the
    node does not otherwise need.

    Failures are counted and logged, never raised: the node re-submits on its
    next tick, because ``confirmed`` still reports the older payload.
    """

    def __init__(self, uploader, log_error=None, max_slots=4,
                 name='s3-upload'):
        """Start the worker for `uploader`, reporting failures to `log_error`.

        `log_error(key, exception)` is called ON THE WORKER THREAD.
        """
        self._uploader = uploader
        self._log_error = log_error
        self._max_slots = max(1, int(max_slots))
        self._lock = threading.Lock()
        self._pending = {}          # key -> (payload, content_type, cache)
        self._confirmed = {}        # key -> tag of the last successful PUT
        self._writes = 0
        self._failures = 0
        self._dropped = 0
        self._wake = threading.Event()
        self._stop = threading.Event()
        # daemon: a hung PUT cannot be cancelled, so the guarantee this class
        # makes about shutdown is that `stop` RETURNS, not that the thread
        # ends. The interpreter must not wait on it at exit either.
        self._worker = threading.Thread(target=self._run, name=name,
                                        daemon=True)
        self._worker.start()

    def submit(self, payload, key, content_type, cache_control, tag=None):
        """Queue `payload` for `key`, replacing any pending one; never blocks.

        Returns True if it was accepted. `tag` is an opaque value -- the two
        callers pass the fix stamp -- reported by `confirmed` once this
        payload is actually on the wire, which is how a caller tells a
        successful upload from an accepted one.
        """
        with self._lock:
            full = len(self._pending) >= self._max_slots
            if full and key not in self._pending:
                self._dropped += 1
                return False
            self._pending[key] = (payload, content_type, cache_control, tag)
        self._wake.set()
        return True

    def confirmed(self, key):
        """Return the `tag` of the last payload SUCCESSFULLY put to `key`."""
        with self._lock:
            return self._confirmed.get(key)

    def counts(self):
        """Return (writes, failures, dropped) as a consistent snapshot."""
        with self._lock:
            return self._writes, self._failures, self._dropped

    def stop(self, timeout=5.0):
        """Stop the worker; return True if it ended within `timeout`.

        Anything still pending is abandoned. That is the right trade for
        this node's objects: they are snapshots at a 1 s and 30 s cadence,
        so the last one is worth strictly less than a shutdown that hangs.
        (coverage_renderer's tiles are NOT snapshots, which is why it flushes
        instead -- see its `stop`.)
        """
        self._stop.set()
        self._wake.set()
        self._worker.join(timeout=timeout)
        return not self._worker.is_alive()

    def _run(self):
        """Send whatever is pending, newest per key, until stopped."""
        while not self._stop.is_set():
            self._wake.wait()
            self._wake.clear()
            while not self._stop.is_set():
                with self._lock:
                    if not self._pending:
                        break
                    # dict preserves insertion order and re-assigning an
                    # existing key does not move it, so this is FIFO across
                    # keys while staying latest-wins within one: a busy
                    # position key cannot starve the track.
                    key = next(iter(self._pending))
                    payload, content_type, cache_control, tag = (
                        self._pending.pop(key))
                self._send(key, payload, content_type, cache_control, tag)

    def _send(self, key, payload, content_type, cache_control, tag):
        """Put one object, recording the outcome for the caller to read."""
        ok, exc = self._uploader.put(payload, key, content_type,
                                     cache_control)
        with self._lock:
            if ok:
                self._writes += 1
                self._confirmed[key] = tag
            else:
                self._failures += 1
        if not ok and self._log_error is not None:
            self._log_error(key, exc)
