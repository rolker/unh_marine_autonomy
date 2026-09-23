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
