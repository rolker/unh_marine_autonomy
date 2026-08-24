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

"""Pin the chart-tile sync: what it skips, what it uploads, and how fast.

`refresh_chart_tiles.py` reimplements what `aws s3 sync` used to do, and the
comparison rule is the part that can fail silently. The tile prefix is FIXED
(`--name`, default `bathy4m`), and the script deliberately re-renders into that
same prefix when the colour ramp changes -- so a rule that skipped a
same-length-but-recoloured PNG would serve stale colours indefinitely. That is
the failure this file exists to make impossible.

The script is loaded by path: it imports nothing from `marine_web_view` on
purpose, so it runs from cron without the ROS overlay sourced.
"""

import hashlib
import importlib.util
import os
import threading
import time

_TEST_DIR = os.path.dirname(__file__)
_SCRIPT = os.path.join(os.path.dirname(_TEST_DIR), 'scripts',
                       'refresh_chart_tiles.py')


def _tile_args(script):
    """Return the ExtraArgs main() actually uploads chart tiles with."""
    return script.TILE_EXTRA_ARGS


def _load_script():
    """Import refresh_chart_tiles.py by path (stdlib-only, safe to import)."""
    spec = importlib.util.spec_from_file_location('refresh_chart_tiles',
                                                  _SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class _FakeS3:
    """Enough of an S3 client for sync_dir: a listing and a recorded PUT."""

    def __init__(self, existing=None, fail_keys=(), delay=0.0):
        # existing: {key: etag-as-stored-by-S3, quoted like the real API}
        self.existing = dict(existing or {})
        self.fail_keys = set(fail_keys)
        self.delay = delay
        self.puts = []
        self.threads = []
        self._lock = threading.Lock()

    # -- listing ---------------------------------------------------------
    def get_paginator(self, operation):
        assert operation == 'list_objects_v2'
        return self

    def paginate(self, Bucket, Prefix):
        contents = [{'Key': key, 'ETag': etag}
                    for key, etag in sorted(self.existing.items())
                    if key.startswith(Prefix)]
        # Two pages, so a single-page implementation cannot pass by accident.
        yield {'Contents': contents[:1]}
        yield {'Contents': contents[1:]}

    # -- upload ----------------------------------------------------------
    def put_object(self, Bucket, Key, Body, **extra):
        if self.delay:
            time.sleep(self.delay)
        with self._lock:
            self.threads.append(threading.current_thread().name)
            self.puts.append({'Bucket': Bucket, 'Key': Key, 'Body': Body,
                              **extra})
        if Key in self.fail_keys:
            raise RuntimeError('AccessDenied')
        return {}


def _write(directory, relative, payload):
    """Write payload at directory/relative, creating parents."""
    path = os.path.join(directory, *relative.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as handle:
        handle.write(payload)
    return path


def _etag(payload):
    """Return the quoted MD5 S3 gives a single-part upload of payload."""
    return '"{}"'.format(hashlib.md5(payload).hexdigest())


def test_an_unchanged_tile_is_skipped(tmp_path):
    """Identical content must not be re-uploaded; that is the whole point."""
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    _write(outdir, '10/1/2.png', b'unchanged-tile-bytes')

    client = _FakeS3({'tiles/bathy4m/10/1/2.png': _etag(b'unchanged-tile-bytes')})
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/', _tile_args(script),
        log=lambda *a: None)

    assert (sent, skipped, failed) == (0, 1, 0)
    assert client.puts == []


def test_a_recoloured_tile_of_the_same_size_is_uploaded(tmp_path):
    """The failure a size-only comparison would cause, made a test.

    A ramp change re-renders into the SAME key. If a same-length repaint were
    skipped, that tile would serve the old colours until the next rule change
    or a --force -- indefinitely, and invisibly.
    """
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    old = b'AAAAAAAAAAAAAAAA'
    new = b'BBBBBBBBBBBBBBBB'
    assert len(old) == len(new), 'the point of this test is equal lengths'
    _write(outdir, '12/9/9.png', new)

    client = _FakeS3({'tiles/bathy4m/12/9/9.png': _etag(old)})
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/', _tile_args(script),
        log=lambda *a: None)

    assert (sent, skipped, failed) == (1, 0, 0), (
        'a repainted tile of identical byte length was skipped: a size-only '
        'comparison would serve stale colours forever')
    assert client.puts[0]['Body'] == new


def test_a_missing_tile_is_uploaded_with_the_cache_policy(tmp_path):
    """Chart tiles are cached hard -- 7 days -- and typed as PNGs."""
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    _write(outdir, '16/100/200.png', b'fresh')

    client = _FakeS3()
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/', _tile_args(script),
        log=lambda *a: None)

    assert (sent, skipped, failed) == (1, 0, 0)
    assert client.puts == [{
        'Bucket': 'unh-ccom-p11-live',
        'Key': 'tiles/bathy4m/16/100/200.png',
        'Body': b'fresh',
        'ContentType': 'image/png',
        'CacheControl': 'public,max-age=604800',
    }]


def test_a_multipart_etag_is_never_trusted_as_a_content_hash(tmp_path):
    """Refuse to trust a multipart ETag as a content hash.

    An S3 ETag equals the object MD5 only for a single-part, non-KMS upload.
    An object left by `aws s3 sync`'s multipart path carries
    '<hash>-<partcount>', which is not the MD5 of anything local. Comparing it
    would be a coin flip, so it has to fall back to uploading.
    """
    script = _load_script()
    assert script.is_content_hash('d41d8cd98f00b204e9800998ecf8427e')
    assert not script.is_content_hash('d41d8cd98f00b204e9800998ecf8427e-3')
    assert not script.is_content_hash('')
    assert not script.is_content_hash('NOTHEXNOTHEXNOTHEXNOTHEXNOTHEXZZ')

    outdir = str(tmp_path / 'bathy4m')
    _write(outdir, '10/1/1.png', b'body')
    client = _FakeS3({'tiles/bathy4m/10/1/1.png':
                      '"{}-2"'.format(hashlib.md5(b'body').hexdigest())})
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/', _tile_args(script),
        log=lambda *a: None)
    assert (sent, skipped, failed) == (1, 0, 0), (
        'an uncomparable ETag must fall back to uploading, not to skipping')


def test_a_failed_upload_is_counted_so_the_manifest_is_withheld(tmp_path):
    """A partial pyramid must not be blessed by a published manifest."""
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    _write(outdir, '10/1/1.png', b'one')
    _write(outdir, '10/1/2.png', b'two')

    client = _FakeS3(fail_keys={'tiles/bathy4m/10/1/2.png'})
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/', _tile_args(script),
        log=lambda *a: None)

    assert (sent, skipped, failed) == (1, 0, 1), (
        'a failing object must be reported, not swallowed: main() gates the '
        'manifest publish on this count')


def test_the_upload_actually_runs_in_parallel(tmp_path):
    """Serial PUTs would make a ~5,839-tile pyramid a quarter-hour upload."""
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    for index in range(8):
        _write(outdir, '10/1/{}.png'.format(index), b'tile-%d' % index)

    client = _FakeS3(delay=0.05)
    started = time.monotonic()
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/', _tile_args(script),
        concurrency=4, log=lambda *a: None)
    elapsed = time.monotonic() - started

    assert (sent, skipped, failed) == (8, 0, 0)
    assert len(set(client.threads)) > 1, (
        'every upload ran on one thread: {}'.format(set(client.threads)))
    assert elapsed < 8 * 0.05, (
        'the uploads did not overlap: {:.2f}s for 8 x 50ms'.format(elapsed))


def test_the_default_concurrency_matches_the_cli_it_replaces():
    """A silent change here is a change in load against one S3 prefix."""
    script = _load_script()
    assert script.DEFAULT_CONCURRENCY == 10


def test_nested_directories_become_slash_separated_keys(tmp_path):
    """The z/x/y.png laid out on disk has to land as z/x/y.png in S3."""
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    _write(outdir, '14/4321/8765.png', b'deep')

    client = _FakeS3()
    script.sync_dir(client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/',
                    _tile_args(script), log=lambda *a: None)
    assert [put['Key'] for put in client.puts] == [
        'tiles/bathy4m/14/4321/8765.png']


def test_chart_tiles_are_cached_for_a_week_as_pngs():
    """The chart pyramid's own cache policy, read off what main() passes.

    These tiles are static for the life of a compilation, so a short max-age
    would put the CDN back in front of CCOM's server for no reason -- which is
    the entire purpose of pre-rendering them.
    """
    script = _load_script()
    assert script.TILE_EXTRA_ARGS == {
        'ContentType': 'image/png',
        'CacheControl': 'public,max-age=604800',
    }


def test_the_manifest_is_published_with_no_cache(tmp_path):
    """The manifest's whole job is to be current, so it must not be cached."""
    script = _load_script()
    path = str(tmp_path / 'manifest.json')
    with open(path, 'wb') as handle:
        handle.write(b'{"bathy4m": {}}')

    client = _FakeS3()
    script.save_manifest(client, path)
    assert client.puts == [{
        'Bucket': script.BUCKET,
        'Key': 'tiles/manifest.json',
        'Body': b'{"bathy4m": {}}',
        'ContentType': 'application/json',
        'CacheControl': 'no-cache',
    }]


def test_an_unreadable_manifest_reads_as_empty(tmp_path):
    """First run, or a transient error: re-render, never abort."""
    script = _load_script()

    class _Broken:
        """A client whose get_object always fails."""

        def get_object(self, **kwargs):
            raise RuntimeError('NoSuchKey')

    assert script.load_manifest(_Broken()) == {}


def test_force_reuploads_unchanged_tiles(tmp_path):
    """The only way a TILE_EXTRA_ARGS cache-policy change reaches a tile.

    `list_objects_v2` returns no CacheControl, so a content comparison cannot
    see that an object's cache policy is stale. `--force` is the remedy that
    exists today, and it is worth nothing if it only re-renders.
    """
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    _write(outdir, '10/1/2.png', b'unchanged-tile-bytes')

    client = _FakeS3({'tiles/bathy4m/10/1/2.png':
                      _etag(b'unchanged-tile-bytes')})
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/',
        _tile_args(script), log=lambda *a: None, force=True)

    assert (sent, skipped, failed) == (1, 0, 0), (
        'force skipped a byte-identical tile, so a cache-policy change has '
        'no way to reach it')
    assert client.puts[0]['CacheControl'] == 'public,max-age=604800'


def test_a_sync_past_its_deadline_stops_instead_of_overrunning(tmp_path):
    """`aws s3 sync` was capped at 3600 s by subprocess.run; restore that.

    An unbounded cron run overlaps its successor, and two runs double the
    request rate against CCOM's server. Abandoned objects count as failures
    so main() withholds the manifest and the next run finishes the job.
    """
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    for index in range(3):
        _write(outdir, '10/1/{}.png'.format(index), b'tile-%d' % index)

    client = _FakeS3()
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/',
        _tile_args(script), log=lambda *a: None, deadline_seconds=-1)

    assert (sent, skipped, failed) == (0, 0, 3), (
        'the aggregate deadline did not stop the fan-out')
    assert client.puts == [], 'an object was PUT after the deadline'
    assert script.SYNC_DEADLINE_SECONDS == 3600, (
        'the default aggregate deadline no longer matches the cap the '
        'subprocess shell-out enforced')


def test_a_second_run_for_the_same_name_is_locked_out(tmp_path):
    """Cron re-entry is the case: one run takes most of an hour.

    Two overlapping runs double the request rate against CCOM and interleave
    writes into the same --workdir.
    """
    script = _load_script()
    workdir = str(tmp_path / 'p11-tiles')

    first = script.acquire_run_lock(workdir, 'bathy4m')
    assert first is not None
    assert script.acquire_run_lock(workdir, 'bathy4m') is None, (
        'a second concurrent run was allowed to start')
    # A different --name is a deliberate operator choice and may run.
    other = script.acquire_run_lock(workdir, 'bathy8m')
    assert other is not None
    other.close()

    first.close()
    assert script.acquire_run_lock(workdir, 'bathy4m') is not None, (
        'the lock outlived the run that held it')


def test_one_s3_request_stays_under_the_cap_the_shell_out_enforced(
        tmp_path, monkeypatch):
    """Restore the three process-level caps `subprocess.run` used to enforce.

    botocore counts max_attempts as TOTAL attempts and retries connect and
    read timeouts alike, so the worst case for one request is
    attempts * (connect + read). The client is built from the ceiling rather
    than the other way round; this pins that the arithmetic actually lands on
    the old numbers.
    """
    import sys
    import types

    script = _load_script()
    built = []

    class _Config:
        def __init__(self, **kwargs):
            self.kwargs = kwargs

    class _Session:
        def __init__(self, profile_name=None):
            self.profile_name = profile_name

        def client(self, service, config=None):
            built.append((self.profile_name, config.kwargs))
            return object()

    boto3_mod = types.ModuleType('boto3')
    boto3_mod.Session = _Session
    botocore = types.ModuleType('botocore')
    config_mod = types.ModuleType('botocore.config')
    config_mod.Config = _Config
    monkeypatch.setitem(sys.modules, 'boto3', boto3_mod)
    monkeypatch.setitem(sys.modules, 'botocore', botocore)
    monkeypatch.setitem(sys.modules, 'botocore.config', config_mod)

    for ceiling, attempts in ((script.MANIFEST_WRITE_SECONDS, 3),
                              (script.MANIFEST_READ_SECONDS, 2)):
        script.s3_client('ccom-jhc', ceiling, attempts=attempts)
        profile, kwargs = built[-1]
        assert profile == 'ccom-jhc'
        worst_case = (kwargs['retries']['max_attempts']
                      * (kwargs['connect_timeout'] + kwargs['read_timeout']))
        assert worst_case == ceiling, (
            'one request can take {} s against a {} s cap'.format(
                worst_case, ceiling))

    assert (script.MANIFEST_WRITE_SECONDS, script.MANIFEST_READ_SECONDS) == (
        120, 60), 'the caps no longer match the subprocess timeouts they replace'
