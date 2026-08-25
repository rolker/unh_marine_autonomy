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
import io
import json
import os
import stat
import threading
import time

import pytest

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


class _S3Error(Exception):
    """Shaped like botocore's ClientError, without needing botocore.

    The script reads the S3 error code off `response` rather than importing
    botocore (it must stay importable without the SDK), so a double that
    wants to be a genuine 404 -- as opposed to a transient failure -- has to
    carry one.
    """

    def __init__(self, code):
        super().__init__(code)
        self.response = {'Error': {'Code': code, 'Message': code}}


class _Store:
    """The shared manifest object, as S3 holds it: one body, read and written.

    `error` scripts the next GET to fail, which is how the transient-read
    case is reproduced from inside the lock.
    """

    def __init__(self, error=None):
        self.body = None
        self.error = error

    def get_object(self, Bucket, Key):
        if self.error is not None:
            raise self.error
        if self.body is None:
            raise _S3Error('NoSuchKey')
        return {'Body': io.BytesIO(self.body)}

    def put_object(self, Bucket, Key, Body, **kwargs):
        self.body = Body


def _write(directory, relative, payload):
    """Write payload at directory/relative, creating parents."""
    path = os.path.join(directory, *relative.split('/'))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as handle:
        handle.write(payload)
    return path


def _etag(payload):
    """Return the quoted MD5 S3 gives a single-part upload of payload."""
    return '"{}"'.format(
        hashlib.md5(payload, usedforsecurity=False).hexdigest())


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
                      '"{}-2"'.format(
                          hashlib.md5(b'body',
                                      usedforsecurity=False).hexdigest())})
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


def test_the_manifest_is_published_with_no_cache():
    """The manifest's whole job is to be current, so it must not be cached."""
    script = _load_script()
    client = _FakeS3()
    script.save_manifest(client, b'{"bathy4m": {}}')
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


def test_every_page_of_the_listing_is_read(tmp_path):
    """A single-page read would re-upload ~4,800 unchanged tiles every run.

    `list_objects_v2` pages at 1,000 keys and this pyramid is ~5,839 objects,
    so stopping at the first page is a silent, expensive regression rather
    than a visible failure -- the run still succeeds, it just uploads
    everything. The fake pages deliberately, so more than one key is what
    makes the paginator loop load-bearing.
    """
    script = _load_script()
    outdir = str(tmp_path / 'bathy4m')
    existing = {}
    for index in range(4):
        payload = b'tile-%d' % index
        _write(outdir, '11/3/{}.png'.format(index), payload)
        existing['tiles/bathy4m/11/3/{}.png'.format(index)] = _etag(payload)

    client = _FakeS3(existing)
    sent, skipped, failed = script.sync_dir(
        client, outdir, 'unh-ccom-p11-live', 'tiles/bathy4m/',
        _tile_args(script), log=lambda *a: None)

    assert (sent, skipped, failed) == (0, 4, 0), (
        'keys past the first listing page were treated as absent and '
        're-uploaded')
    assert client.puts == []


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
    writes into the same --workdir. With no per-request ceiling to appeal to,
    this lock is the ONLY thing bounding overlap.
    """
    script = _load_script()
    directory = str(tmp_path / 'locks')
    os.makedirs(directory)

    first = script.acquire_run_lock('bathy4m', directory=directory)
    with pytest.raises(script.RunLockHeld):
        script.acquire_run_lock('bathy4m', directory=directory)
    # A different --name is a deliberate operator choice and may run.
    other = script.acquire_run_lock('bathy8m', directory=directory)
    other.close()

    first.close()
    script.acquire_run_lock('bathy4m', directory=directory).close()


def test_a_held_lock_names_its_holder_and_its_age(tmp_path):
    """A wedged lock must not stop every future run silently.

    Exiting 0 on a held lock is right for an ordinary overlap and wrong
    forever after a run wedges: every later invocation would report success
    while doing nothing. The holder's pid and start time are written into the
    lock file so the two cases can be told apart.
    """
    script = _load_script()
    directory = str(tmp_path / 'locks')
    os.makedirs(directory)

    held = script.acquire_run_lock('bathy4m', directory=directory)
    try:
        with pytest.raises(script.RunLockHeld) as raised:
            script.acquire_run_lock('bathy4m', directory=directory)
        assert raised.value.holder == os.getpid()
        assert raised.value.age is not None and raised.value.age < 60
    finally:
        held.close()

    # An unreadable holder record is still a held lock, not a crash.
    with open(os.path.join(directory, 'bathy4m.lock'), 'w') as handle:
        handle.write('nonsense')
    held = script.acquire_run_lock('bathy4m', directory=directory)
    try:
        with pytest.raises(script.RunLockHeld) as raised:
            script.acquire_run_lock('bathy4m', directory=directory)
    finally:
        held.close()


def test_deleting_the_lock_file_does_not_release_the_lock(tmp_path):
    """What the README must tell the operator, pinned.

    `flock` is held on the INODE. Unlinking the path releases nothing, and
    the next run creates a fresh inode and acquires immediately -- so an
    operator told to "delete the lock file" gets two concurrent crawls of
    CCOM's server, which is precisely what the lock exists to prevent. The
    remedy is to kill the holder; the kernel releases the lock when the
    process exits.
    """
    script = _load_script()
    directory = str(tmp_path / 'locks')
    os.makedirs(directory)

    held = script.acquire_run_lock('bathy4m', directory=directory)
    try:
        os.unlink(os.path.join(directory, 'bathy4m.lock'))
        # Does NOT raise RunLockHeld -- that is the whole hazard. Two runs
        # now hold a lock each, on two different inodes, and both crawl.
        second = script.acquire_run_lock('bathy4m', directory=directory)
        try:
            assert not held.closed, 'the first run released anything'
            assert second.fileno() != held.fileno()
        finally:
            second.close()
    finally:
        held.close()


def test_the_lock_directory_refuses_a_tree_this_user_does_not_own(
        tmp_path, monkeypatch):
    """The lock must not live in a world-writable tree.

    --workdir defaults under /tmp. `makedirs(exist_ok=True)` accepts a
    directory somebody else made, and a symlink planted where the lock file
    goes was demonstrated to truncate its target. The same tree is `outdir`,
    so anything landing in it is PUT to the public tiles/ prefix under an
    admin profile.
    """
    script = _load_script()
    monkeypatch.setenv('XDG_RUNTIME_DIR', str(tmp_path))
    directory = script.lock_dir()
    assert directory.startswith(str(tmp_path))
    assert stat.S_IMODE(os.lstat(directory).st_mode) & (
        stat.S_IWGRP | stat.S_IWOTH) == 0

    os.chmod(directory, 0o777)
    with pytest.raises(RuntimeError):
        script.lock_dir()

    # A symlink where the lock file belongs is refused, not followed.
    os.chmod(directory, 0o700)
    target = tmp_path / 'precious'
    target.write_text('do not truncate me')
    os.symlink(str(target), os.path.join(directory, 'bathy4m.lock'))
    with pytest.raises(OSError):
        script.acquire_run_lock('bathy4m', directory=directory)
    assert target.read_text() == 'do not truncate me'


def test_the_lock_directory_is_not_the_workdir(tmp_path, monkeypatch):
    """A file in --workdir is a file in `outdir`'s parent, and gets PUT."""
    script = _load_script()
    monkeypatch.setenv('XDG_RUNTIME_DIR', str(tmp_path / 'run'))
    os.makedirs(str(tmp_path / 'run'))
    assert '/tmp/p11-tiles' not in script.lock_dir()


def test_two_names_do_not_erase_each_other_from_the_manifest(
        tmp_path, monkeypatch):
    """The lost update that costs a full re-crawl of CCOM.

    `tiles/manifest.json` is one object describing every --name and the run
    lock is per --name, so two names running together -- which the lock
    deliberately permits -- each read the whole dict at the start of their
    run and wrote the whole dict back at the end. The loser's entry vanished,
    and the next cron run read "never rendered" and re-fetched ~5,839 tiles.
    """
    script = _load_script()
    directory = str(tmp_path / 'locks')
    os.makedirs(directory)
    workdir = str(tmp_path / 'work')
    os.makedirs(workdir)

    store = _Store()
    # The stale dict each run read an hour ago, before the other published.
    script.update_manifest(store, 'bathy4m', {'tiles': 1},
                           directory=directory)
    script.update_manifest(store, 'bathy8m', {'tiles': 2},
                           directory=directory)

    assert json.loads(store.body) == {'bathy4m': {'tiles': 1},
                                      'bathy8m': {'tiles': 2}}, (
        'one name erased the other from the shared manifest')
    # And nothing was staged through --workdir on the way: that tree is
    # world-writable /tmp by default, and it is also the tree whose contents
    # are PUT to the public tiles/ prefix.
    assert os.listdir(workdir) == [], (
        'the manifest was staged through the unowned workdir')
    # Nor through any other file: the merged JSON goes to S3 from memory, so
    # the only thing on disk is the lock itself.
    assert sorted(os.listdir(directory)) == ['manifest.lock'], (
        os.listdir(directory))


def test_a_transient_read_inside_the_lock_does_not_erase_the_others(
        tmp_path):
    """The lost update again, from one failed GET instead of a race.

    `load_manifest` answers {} for anything it cannot read, so merging this
    run's entry into that answer PUTs a manifest holding ONE --name and drops
    every other -- inside the lock that exists to stop exactly that, and at
    the ~5,839-tile re-crawl the docstring quotes. A read that failed is not
    a manifest that is empty, and this run's entry is worth less than every
    other name's.
    """
    script = _load_script()
    directory = str(tmp_path / 'locks')
    os.makedirs(directory)

    store = _Store()
    script.update_manifest(store, 'bathy4m', {'tiles': 1},
                           directory=directory)
    published = store.body

    store.error = _S3Error('SlowDown')
    with pytest.raises(Exception) as failure:
        script.update_manifest(store, 'bathy8m', {'tiles': 2},
                               directory=directory)
    assert not isinstance(failure.value, AssertionError)
    assert store.body == published, (
        'a manifest was published over a read that failed, erasing every '
        'other --name'
    )

    # A genuinely absent object is still the first-run case, not an error.
    store.error = None
    store.body = None
    assert script.update_manifest(store, 'bathy8m', {'tiles': 2},
                                  directory=directory) == {
        'bathy8m': {'tiles': 2}}


def test_a_manifest_that_is_not_an_object_is_refused_not_overwritten(
        tmp_path):
    """Whatever is there is the only record of what the other names published."""
    script = _load_script()
    directory = str(tmp_path / 'locks')
    os.makedirs(directory)

    store = _Store()
    store.body = b'["not", "a", "manifest"]'
    with pytest.raises(RuntimeError):
        script.update_manifest(store, 'bathy4m', {'tiles': 1},
                               directory=directory)
    assert store.body == b'["not", "a", "manifest"]'

    store.body = b'{truncated'
    with pytest.raises(Exception):
        script.update_manifest(store, 'bathy4m', {'tiles': 1},
                               directory=directory)
    assert store.body == b'{truncated'


def test_the_run_start_read_stays_forgiving():
    """At the top of a run there is nothing to lose by re-rendering.

    The strict read is for the read-modify-write alone: making the opening
    read fatal would stop a cron run on a transient GET when re-crawling is
    the correct, if expensive, answer.
    """
    script = _load_script()

    class _Broken:
        """A client whose get_object always fails, and not with a 404."""

        def get_object(self, **kwargs):
            raise RuntimeError('connection reset by peer')

    assert script.load_manifest(_Broken()) == {}
    with pytest.raises(RuntimeError):
        script.load_manifest(_Broken(), strict=True)


def test_the_client_config_is_what_reaches_botocore(tmp_path, monkeypatch):
    """Pin the Config, without asserting a per-request wall-clock ceiling.

    An earlier round derived read_timeout by solving
    `attempts * (connect + read) == cap`, and pinned the arithmetic with a
    test that held identically for any CONNECT_TIMEOUT. The ceiling was not
    real -- urllib3 applies connect_timeout per address and the endpoint has
    several -- so the derivation is gone and what is left to bind is what
    actually reaches botocore.
    """
    import sys
    import types

    script = _load_script()
    built = []

    class _Config:
        """Record the botocore Config kwargs instead of validating them."""

        def __init__(self, **kwargs):
            self.kwargs = kwargs

    class _Session:
        """Record the profile and the config the client is built with."""

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

    script.s3_client('ccom-jhc')
    assert built[-1] == ('ccom-jhc', {
        'retries': {'mode': 'standard', 'max_attempts': 3},
        'connect_timeout': 10,
        'read_timeout': 30,
    }), built[-1]
    assert (script.REQUEST_ATTEMPTS, script.CONNECT_TIMEOUT,
            script.READ_TIMEOUT) == (3, 10, 30)

    # Retries stay ON here, unlike the nodes: a cron run gets no second
    # chance for hours and one failed PUT withholds the whole manifest.
    assert built[-1][1]['retries']['max_attempts'] > 1

    for bad in ({'attempts': 0}, {'attempts': -1}, {'read_timeout': 0},
                {'read_timeout': -5}):
        with pytest.raises(ValueError):
            script.s3_client('ccom-jhc', **bad)


def _run_main(script, monkeypatch, tmp_path, argv, held=None):
    """Run `main()` over stubbed CCOM and S3, returning what it wired up.

    `main()` had no test at all, which is how six mutations at its call sites
    survived a green suite: a dropped `force=a.force`, a lock taken for a
    constant instead of `--name`, a held-lock branch turned into `if False`.
    Every one of those is a line in this function's caller.
    """
    import sys

    seen = {'sync': [], 'manifest': [], 'locks': []}
    locks = str(tmp_path / 'locks')
    os.makedirs(locks, exist_ok=True)

    def _lock_dir():
        return locks

    real_lock = script.acquire_run_lock

    def _acquire(name, directory=None):
        seen['locks'].append(name)
        return real_lock(name, directory=directory)

    def _sync(client, local_dir, bucket, prefix, extra, **kwargs):
        seen['sync'].append((prefix, extra, kwargs))
        return 1, 0, 0

    def _update(client, name, entry, **kwargs):
        seen['manifest'].append((name, entry))
        return {name: entry}

    monkeypatch.setattr(script, 'lock_dir', _lock_dir)
    monkeypatch.setattr(script, 'acquire_run_lock', _acquire)
    monkeypatch.setattr(script, 'sync_dir', _sync)
    monkeypatch.setattr(script, 'update_manifest', _update)
    monkeypatch.setattr(script, 's3_client', lambda *a, **k: object())
    monkeypatch.setattr(script, 'load_manifest', lambda client: {})
    monkeypatch.setattr(
        script, 'pick_service',
        lambda layer, bbox, timeout, blank: ('BTY_4m', 'http://ccom/tiles'))
    monkeypatch.setattr(
        script, 'fetch_tile',
        lambda endpoint, z, x, y, timeout: b'x' * 2000)
    monkeypatch.setattr(sys, 'argv', ['refresh_chart_tiles.py'] + argv)
    seen['exit'] = script.main()
    return seen


_MAIN_ARGV = ['--bbox', '-70.61', '43.06', '-70.60', '43.07',
              '--zmin', '10', '--zmax', '10', '--rate', '1000',
              '--name', 'bathy4m']


def test_main_passes_force_through_to_the_upload(tmp_path, monkeypatch):
    """`--force` is the ONLY way a cache-policy change reaches a tile.

    `sync_dir` skips any object whose remote ETag matches its MD5, and a
    `TILE_EXTRA_ARGS` change moves no bytes -- so dropping `force=a.force`
    here leaves the prefix on a permanently mixed cache policy with nothing
    in the suite noticing. That mutation survived the last round.
    """
    script = _load_script()
    argv = _MAIN_ARGV + ['--workdir', str(tmp_path / 'work')]

    seen = _run_main(script, monkeypatch, tmp_path, argv)
    assert seen['exit'] == 0
    assert seen['sync'][0][2].get('force') is False, seen['sync']

    seen = _run_main(script, monkeypatch, tmp_path, argv + ['--force'])
    assert seen['sync'][0][2].get('force') is True, (
        '--force no longer reaches the upload, so a TILE_EXTRA_ARGS change '
        'has no way to propagate')


def test_main_locks_and_publishes_under_the_requested_name(
        tmp_path, monkeypatch):
    """The lock and the manifest entry must both follow --name."""
    script = _load_script()
    seen = _run_main(script, monkeypatch, tmp_path,
                     ['--bbox', '-70.61', '43.06', '-70.60', '43.07',
                      '--zmin', '10', '--zmax', '10', '--rate', '1000',
                      '--name', 'bathy8m',
                      '--workdir', str(tmp_path / 'work')])
    assert seen['locks'] == ['bathy8m'], seen['locks']
    assert seen['sync'][0][0] == 'tiles/bathy8m/', seen['sync']
    assert [name for name, _ in seen['manifest']] == ['bathy8m']
    assert seen['sync'][0][1] == script.TILE_EXTRA_ARGS


def test_main_stops_before_touching_ccom_when_the_lock_is_held(
        tmp_path, monkeypatch):
    """The whole point of taking the lock first.

    Probing CCOM before checking the lock would already have doubled the
    request rate. A held lock exits 0 -- an ordinary overlap is the lock
    working -- but a holder too old to be a real run exits non-zero, so a
    wedged lock is not indistinguishable from success forever.
    """
    script = _load_script()
    locks = str(tmp_path / 'locks')
    os.makedirs(locks)
    monkeypatch.setattr(script, 'lock_dir', lambda: locks)
    held = script.acquire_run_lock('bathy4m', directory=locks)
    try:
        def _explode(*args, **kwargs):
            raise AssertionError('a locked-out run reached CCOM')

        monkeypatch.setattr(script, 'pick_service', _explode)
        monkeypatch.setattr(script, 'fetch_tile', _explode)
        import sys
        monkeypatch.setattr(sys, 'argv', ['refresh_chart_tiles.py']
                            + _MAIN_ARGV + ['--workdir', str(tmp_path / 'w')])
        assert script.main() == 0, 'an ordinary overlap is not a failure'

        # Now the holder looks wedged: same lock, an older start time.
        with open(os.path.join(locks, 'bathy4m.lock'), 'r+') as handle:
            handle.seek(0)
            handle.write('{} {}\n'.format(
                os.getpid(), int(time.time() - script.LOCK_STALE_SECONDS - 1)))
            handle.truncate()
        assert script.main() == 1, (
            'a lock older than LOCK_STALE_SECONDS still exited 0, so a '
            'wedged lock reads as success forever')
    finally:
        held.close()


def test_the_sync_deadline_default_is_the_one_main_relies_on(tmp_path):
    """`sync_dir`'s default IS the aggregate bound; nothing else passes one."""
    script = _load_script()
    import inspect
    default = inspect.signature(script.sync_dir).parameters[
        'deadline_seconds'].default
    assert default == script.SYNC_DEADLINE_SECONDS == 3600, default


def test_the_wedged_lock_threshold_is_longer_than_a_real_run():
    """A full crawl at the default --rate is well under an hour.

    Too short and an ordinary long run starts mailing failures; too long and
    a wedged lock keeps reading as success. Neither is visible from the code
    that uses the constant, so pin the value.
    """
    script = _load_script()
    assert script.LOCK_STALE_SECONDS == 6 * 3600
