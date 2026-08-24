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
from marine_web_view.s3_upload import describe_error
from marine_web_view.s3_upload import S3Uploader

import pytest


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

    coverage_renderer passes `profile or None` so an unset profile means the
    default credential chain (an EC2 instance role); state_renderer passes its
    profile through so a blanked one fails loudly. Both depend on this helper
    not second-guessing the value on its way to boto3.Session.
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
    """state_renderer's own cache policy, pinned on the new seam.

    The position object's max-age IS the publish interval: hold it longer and
    a viewer sits on a stale fix; the object is typed application/geo+json so
    the page's fetch parses it as GeoJSON. Neither survives a `_put` that
    drops what it is handed.
    """
    from marine_web_view.state_renderer import StateRenderer

    client = _Client()

    class _Node:
        bucket = 'unh-ccom-p11-live'
        _failures = 0
        _uploader = S3Uploader('unh-ccom-p11-live', client=client)

        def get_logger(self):
            raise AssertionError('the upload should not have failed')

    node = _Node()
    assert StateRenderer._put(node, '{"type": "Feature"}',
                              'live/position.geojson', 1.0) is True
    assert client.calls == [{
        'Bucket': 'unh-ccom-p11-live',
        'Key': 'live/position.geojson',
        'Body': b'{"type": "Feature"}',
        'ContentType': 'application/geo+json',
        'CacheControl': 'max-age=1',
    }]


def test_a_failed_position_upload_is_counted_not_raised():
    """The upload runs on a ROS timer; a raise there stops publishing."""
    logged = []

    class _Logger:
        """Capture the error line the node logs."""

        def error(self, message, **kwargs):
            logged.append(message)

    from marine_web_view.state_renderer import StateRenderer

    class _Node:
        bucket = 'unh-ccom-p11-live'
        _failures = 0
        _uploader = S3Uploader(
            'unh-ccom-p11-live',
            client=_Client(error=_ClientError('AccessDenied', 'nope')))

        def get_logger(self):
            return _Logger()

    node = _Node()
    assert StateRenderer._put(node, '{}', 'live/position.geojson', 1.0) is False
    assert node._failures == 1
    assert logged and 'AccessDenied' in logged[0], logged


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
