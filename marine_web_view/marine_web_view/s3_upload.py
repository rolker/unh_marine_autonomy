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


def _boto3_client(profile, connect_timeout, read_timeout):
    """Build an S3 client and the exception classes its calls can raise.

    Returns ``(client, transport_errors)``. The profile is passed through
    EXACTLY as given: each caller decides whether an unset profile means
    ``None`` (fall through to the default credential chain -- an instance
    role, or a plain ~/.aws/credentials) or the empty string (an operator
    mistake that should fail). Coalescing here would erase that distinction.

    ``retries`` turns on botocore's own exponential backoff for transient
    errors (throttling, 5xx, connection resets). This is strictly additive:
    the AWS CLI shell-out it replaces got no automatic retry of any kind.
    Anything that exhausts it, or is not retryable at all (AccessDenied,
    NoSuchBucket), still falls through to the caller's failure counter.
    """
    import boto3
    from botocore.config import Config
    from botocore.exceptions import BotoCoreError, ClientError

    config = Config(connect_timeout=connect_timeout,
                    read_timeout=read_timeout,
                    retries={'mode': 'standard', 'max_attempts': 4})
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
