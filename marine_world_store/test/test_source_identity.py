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

"""Content identity: what changes an id, and what deliberately does not."""

import hashlib

from marine_world_store import source_identity
from marine_world_store.source_identity import SourceIdentityError
import pytest


def make_bag(directory, splits=('rosbag2_0.mcap', 'rosbag2_1.mcap'),
             payload=b'ping'):
    """Build a bag-shaped directory: numbered splits plus a metadata.yaml."""
    directory.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(splits):
        (directory / name).write_bytes(payload + bytes([index]))
    (directory / 'metadata.yaml').write_text('rosbag2_bagfile_information:\n')
    return directory


def test_file_key_is_the_git_annex_shape(tmp_path):
    """``SHA256E-s<size>--<hex><ext>`` -- git-annex's own key format."""
    path = tmp_path / 'cast.asvp'
    path.write_bytes(b'12345')
    key = source_identity.file_key(path)
    expected = hashlib.sha256(b'12345').hexdigest()
    assert key == f'SHA256E-s5--{expected}.asvp'


def test_file_key_keeps_the_extension(tmp_path):
    """
    Keep the extension, as the ``E`` in SHA256E means.

    An id computed before annexing must equal the one computed after.
    """
    path = tmp_path / 'rosbag2_0.mcap'
    path.write_bytes(b'x')
    assert source_identity.file_key(path).endswith('.mcap')


def test_bag_id_is_stable(tmp_path):
    """The same bytes give the same id, twice."""
    bag = make_bag(tmp_path / 'bag')
    assert source_identity.bag_source_id(bag) == \
        source_identity.bag_source_id(bag)


def test_reindex_does_not_change_the_id(tmp_path):
    """
    Leave the id alone when only ``metadata.yaml`` changes.

    It is excluded, so a reindex is a repair rather than a new source
    (design section 3, verified behaviour).
    """
    bag = make_bag(tmp_path / 'bag')
    before = source_identity.bag_source_id(bag)
    (bag / 'metadata.yaml').write_text('rewritten: by ros2 bag reindex\n')
    assert source_identity.bag_source_id(bag) == before


def test_other_files_are_excluded(tmp_path):
    """Exclude everything but sensor data: a stray note changes nothing."""
    bag = make_bag(tmp_path / 'bag')
    before = source_identity.bag_source_id(bag)
    (bag / 'notes.txt').write_text('operator log')
    (bag / 'thumbnail.png').write_bytes(b'\x89PNG')
    assert source_identity.bag_source_id(bag) == before


def test_changed_payload_changes_the_id(tmp_path):
    """The whole point: identity is content."""
    first = make_bag(tmp_path / 'a')
    second = make_bag(tmp_path / 'b', payload=b'pong')
    assert source_identity.bag_source_id(first) != \
        source_identity.bag_source_id(second)


def test_split_names_are_part_of_the_id(tmp_path):
    """Split order is meaningful, so the filenames are hashed too."""
    first = make_bag(tmp_path / 'a', splits=('rosbag2_0.mcap',))
    second = make_bag(tmp_path / 'b', splits=('rosbag2_9.mcap',))
    assert source_identity.bag_source_id(first) != \
        source_identity.bag_source_id(second)


def test_a_missing_split_changes_the_id(tmp_path):
    """A truncated bag is a different source; no special case needed."""
    bag = make_bag(tmp_path / 'bag')
    before = source_identity.bag_source_id(bag)
    (bag / 'rosbag2_1.mcap').unlink()
    assert source_identity.bag_source_id(bag) != before


def test_directory_order_does_not_matter(tmp_path):
    """Lines are sorted, so readdir order cannot change an id."""
    bag = make_bag(tmp_path / 'bag')
    lines = source_identity.merkle_lines(bag)
    assert lines == sorted(lines)


def test_a_directory_with_no_data_file_is_an_error(tmp_path):
    """Hashing an empty list would give every empty directory one id."""
    empty = tmp_path / 'empty'
    empty.mkdir()
    (empty / 'metadata.yaml').write_text('')
    with pytest.raises(SourceIdentityError):
        source_identity.bag_source_id(empty)


def test_single_file_source_is_its_file_key(tmp_path):
    """Design section 3: a cast or a prior grid is its key alone."""
    path = tmp_path / 'cast.asvp'
    path.write_bytes(b'profile')
    assert source_identity.file_source_id(path) == \
        source_identity.file_key(path)


def test_source_id_dispatches_on_what_it_is(tmp_path):
    """One entry point for either kind of source."""
    bag = make_bag(tmp_path / 'bag')
    single = tmp_path / 'cast.asvp'
    single.write_bytes(b'profile')
    assert source_identity.source_id(bag) == source_identity.bag_source_id(bag)
    assert source_identity.source_id(single) == \
        source_identity.file_key(single)


def test_missing_paths_are_reported(tmp_path):
    """A path that is not there is a loud error, never an empty hash."""
    with pytest.raises(SourceIdentityError):
        source_identity.file_key(tmp_path / 'nope')
    with pytest.raises(SourceIdentityError):
        source_identity.bag_source_id(tmp_path / 'nope')
