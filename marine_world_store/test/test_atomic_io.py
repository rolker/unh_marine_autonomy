# Copyright 2026 University of New Hampshire
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
#    * Neither the name of the University of New Hampshire nor the names of its
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

"""
Publishing a file whole: a private temporary, fsync, then rename.

Regression for the shared ``<name>.tmp`` every writer here used: two writers
of one file wrote into one temporary, and the first to rename could publish
the other's half-written bytes (or leave the second rename with nothing to
rename).
"""

import os
import stat

from marine_world_store import atomic_io
import pytest


def test_two_writers_of_one_file_never_share_a_temporary(tmp_path):
    """A writer interleaved inside another's write leaves both whole."""
    path = tmp_path / 'item.json'

    def slow_writer(tmp):
        tmp.write_text('A' * 1000)
        # A second writer publishes the same file mid-way through the first.
        atomic_io.write_text(path, 'B')
        assert path.read_text() == 'B'

    atomic_io.publish(path, slow_writer)
    # The first writer's rename lands last, and it publishes ITS content --
    # with a shared temporary it would have found nothing to rename.
    assert path.read_text() == 'A' * 1000
    assert [p.name for p in tmp_path.iterdir()] == ['item.json']


def test_a_failed_fill_publishes_nothing_and_leaves_no_temporary(tmp_path):
    """A writer that raises leaves the previous file exactly as it was."""
    path = tmp_path / 'item.json'
    path.write_text('old')

    def failing(tmp):
        tmp.write_text('half')
        raise RuntimeError('died mid-write')

    with pytest.raises(RuntimeError):
        atomic_io.publish(path, failing)
    assert path.read_text() == 'old'
    assert [p.name for p in tmp_path.iterdir()] == ['item.json']


def test_a_copy_that_fails_its_check_is_never_visible(tmp_path):
    """The byte-identical check runs before the copy is published."""
    source = tmp_path / 'source.tif'
    source.write_bytes(b'pixels')
    target = tmp_path / 'target.tif'

    def refuse(tmp):
        raise ValueError('not identical')

    with pytest.raises(ValueError):
        atomic_io.copy_file(source, target, check=refuse)
    assert not target.exists()
    atomic_io.copy_file(source, target)
    assert target.read_bytes() == b'pixels'


@pytest.mark.parametrize('umask', [0o022, 0o002, 0o077])
def test_a_published_file_gets_the_mode_open_would_give_it(tmp_path, umask):
    """
    0666 less the umask, exactly as a plain ``open()`` would create it.

    Regression: the temporary came from mkstemp (0600) and was renamed as is,
    so every Item, Collection, manifest, .fp and revision was owner-only and
    unreadable to any other uid (renderers, CAMP, ``docker -u``, a NAS sync).
    """
    old = os.umask(umask)
    try:
        published = atomic_io.write_text(tmp_path / 'item.json', '{}')
        plain = tmp_path / 'plain.json'
        with open(plain, 'w') as handle:
            handle.write('{}')
    finally:
        os.umask(old)
    assert stat.S_IMODE(published.stat().st_mode) == 0o666 & ~umask
    assert stat.S_IMODE(published.stat().st_mode) == \
        stat.S_IMODE(plain.stat().st_mode)


@pytest.mark.parametrize('source_mode', [0o600, 0o444, 0o777])
def test_a_copy_gets_the_published_mode_not_its_sources(tmp_path, source_mode):
    """
    copy_file keeps the source's times, never its mode.

    Regression: copy_file was copy2 end to end, so a 0600 source published
    an owner-only tile and a 0444 one a read-only tile, bypassing the
    publish mode.
    """
    source = tmp_path / 'source.tif'
    source.write_bytes(b'pixels')
    os.utime(source, ns=(10**18, 10**18))
    source.chmod(source_mode)
    old = os.umask(0o022)
    try:
        target = atomic_io.copy_file(source, tmp_path / 'target.tif')
    finally:
        os.umask(old)
    assert stat.S_IMODE(target.stat().st_mode) == 0o644
    assert target.stat().st_mtime_ns == 10**18


def test_a_rewrite_keeps_the_files_mode(tmp_path):
    """
    An operator's mode on a published file survives a rewrite.

    Regression: every rewrite reset it to 0666 less the umask, so a g+w
    given to collection.json was lost on the next regenerate.
    """
    path = atomic_io.write_text(tmp_path / 'collection.json', '{}')
    path.chmod(0o664)
    old = os.umask(0o077)
    try:
        atomic_io.write_text(path, '{"a": 1}')
    finally:
        os.umask(old)
    assert stat.S_IMODE(path.stat().st_mode) == 0o664
    assert path.read_text() == '{"a": 1}'


def test_publishing_never_changes_the_process_umask(tmp_path, monkeypatch):
    """
    The umask is the kernel's to apply, not read by setting it.

    Regression: reading it meant os.umask(), which changes it process-wide
    for a moment -- a race with any other thread creating a file.
    """
    def forbidden(mask):
        raise AssertionError('os.umask called')

    monkeypatch.setattr(atomic_io.os, 'umask', forbidden)
    atomic_io.write_text(tmp_path / 'item.json', '{}')


def test_the_data_is_synced_before_the_rename(tmp_path, monkeypatch):
    """Fsync the data, then rename, then the dir: rename is not durable."""
    calls = []
    real_fsync = atomic_io.os.fsync
    real_replace = atomic_io.os.replace

    def fsync(fd):
        calls.append('fsync')
        real_fsync(fd)

    def replace(src, dst):
        calls.append('replace')
        real_replace(src, dst)

    monkeypatch.setattr(atomic_io.os, 'fsync', fsync)
    monkeypatch.setattr(atomic_io.os, 'replace', replace)
    atomic_io.write_text(tmp_path / 'x.json', '{}')
    assert calls[:2] == ['fsync', 'replace']
    assert calls.count('fsync') == 2   # the file, then the directory
