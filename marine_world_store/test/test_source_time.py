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
Where an Item's time comes from: the bag, or a loud refusal.

The rule (operator decision 2026-09-22): every Item carries the observation
interval of the material it is made of, derived from the sources. A product
that cannot be dated is not written.
"""

from marine_world_store import source_time
from marine_world_store.source_time import TimeIntervalError
import pytest

#: One real bag's numbers, in rosbag2's own units.
START_NS = 1781451538661123281
DURATION_NS = 68947054668


def write_metadata(bag_dir, body):
    """Write a metadata.yaml with ``body`` under rosbag2's own key."""
    bag_dir.mkdir(parents=True, exist_ok=True)
    (bag_dir / 'metadata.yaml').write_text(
        'rosbag2_bagfile_information:\n' + body)
    return bag_dir


def a_bag(tmp_path, start_ns=START_NS, duration_ns=DURATION_NS, count=10555):
    """A bag directory carrying rosbag2's start-plus-duration fields."""
    return write_metadata(tmp_path / 'bag', (
        f'  version: 9\n'
        f'  storage_identifier: mcap\n'
        f'  duration:\n    nanoseconds: {duration_ns}\n'
        f'  starting_time:\n'
        f'    nanoseconds_since_epoch: {start_ns}\n'
        f'  message_count: {count}\n'))


def test_the_interval_is_the_recorded_start_plus_duration(tmp_path):
    """The reading rosbag2 itself writes for every bag."""
    start, end = source_time.bag_interval(a_bag(tmp_path))
    assert start == '2026-06-14T15:38:58.661123Z'
    assert end == '2026-06-14T15:40:07.608178Z'


def test_the_end_is_rounded_up_never_short(tmp_path):
    """An interval that ended early would not cover its own last message."""
    start, end = source_time.interval_from_nanoseconds(START_NS, 1)
    assert end > start
    assert end.endswith('661124Z')


def test_the_files_entries_are_the_fallback_reading(tmp_path):
    """A bag with no top-level duration is dated by its splits' range."""
    bag = write_metadata(tmp_path / 'bag', (
        '  message_count: 12\n'
        '  files:\n'
        f'    - path: bag_0.mcap\n'
        f'      starting_time:\n'
        f'        nanoseconds_since_epoch: {START_NS}\n'
        f'      duration:\n        nanoseconds: 1000000000\n'
        f'    - path: bag_1.mcap\n'
        f'      starting_time:\n'
        f'        nanoseconds_since_epoch: {START_NS + 2000000000}\n'
        f'      duration:\n        nanoseconds: 1000000000\n'))
    start, end = source_time.bag_interval(bag)
    assert start == '2026-06-14T15:38:58.661123Z'
    assert end == '2026-06-14T15:39:01.661124Z'


def test_a_bag_with_no_metadata_is_refused(tmp_path):
    """Undated is a provenance defect, not a gap to fill with a guess."""
    bag = tmp_path / 'bag'
    bag.mkdir()
    (bag / 'rosbag2_0.mcap').write_bytes(b'ping')
    with pytest.raises(TimeIntervalError) as caught:
        source_time.bag_interval(bag)
    assert 'metadata.yaml' in str(caught.value)
    assert '--start/--end' in str(caught.value)


def test_metadata_that_is_not_rosbag2_is_named_as_such(tmp_path):
    """Absent metadata and non-rosbag2 metadata need different repairs."""
    bag = tmp_path / 'bag'
    bag.mkdir()
    (bag / 'metadata.yaml').write_text('something_else: 1\n')
    with pytest.raises(TimeIntervalError) as caught:
        source_time.bag_interval(bag)
    assert 'rosbag2_bagfile_information' in str(caught.value)


def test_unparseable_metadata_is_refused(tmp_path):
    """A YAML error is not a missing time."""
    bag = tmp_path / 'bag'
    bag.mkdir()
    (bag / 'metadata.yaml').write_text('rosbag2_bagfile_information: [\n')
    with pytest.raises(TimeIntervalError):
        source_time.bag_interval(bag)


def test_a_bag_that_recorded_nothing_has_no_interval(tmp_path):
    """A recording that observed nothing cannot date a product."""
    with pytest.raises(TimeIntervalError) as caught:
        source_time.bag_interval(a_bag(tmp_path, count=0))
    assert '0 messages' in str(caught.value)


def test_a_zero_start_is_refused(tmp_path):
    """The epoch is what an uninitialised clock writes, not an observation."""
    with pytest.raises(TimeIntervalError):
        source_time.bag_interval(a_bag(tmp_path, start_ns=0))


def test_a_single_file_source_must_be_dated_by_its_caller(tmp_path):
    """A cast file carries no recording metadata; mtime is not a time."""
    path = tmp_path / 'cast.asvp'
    path.write_text('x')
    with pytest.raises(TimeIntervalError) as caught:
        source_time.source_interval(path)
    assert 'modification time' in str(caught.value)


def test_source_interval_reads_a_bag_directory(tmp_path):
    """One entry point for either kind of source."""
    assert source_time.source_interval(a_bag(tmp_path))[0].startswith('2026-')


def test_a_product_takes_the_union_of_its_sources(tmp_path):
    """A tile built from three bags was observed over all three."""
    start, end = source_time.union_intervals([
        ('2026-06-22T14:00:00Z', '2026-06-22T15:00:00Z'),
        ('2026-06-22T13:22:29Z', '2026-06-22T13:50:00Z'),
        ('2026-06-22T14:30:00Z', '2026-06-22T16:10:00Z')])
    assert start == '2026-06-22T13:22:29Z'
    assert end == '2026-06-22T16:10:00Z'


def test_a_union_missing_one_end_is_refused():
    """A dropped source would claim a narrower window than the truth."""
    with pytest.raises(TimeIntervalError):
        source_time.union_intervals([
            ('2026-06-22T14:00:00Z', '2026-06-22T15:00:00Z'),
            ('2026-06-22T13:22:29Z', None)])


def test_a_union_of_nothing_is_refused():
    """Zero sources is not an interval covering everything."""
    with pytest.raises(TimeIntervalError):
        source_time.union_intervals([])


def test_an_interval_must_be_ordered():
    """An end before its start is a defect in whatever produced it."""
    with pytest.raises(TimeIntervalError):
        source_time.check_interval('2026-06-22T15:00:00Z',
                                   '2026-06-22T13:00:00Z')


def test_a_naive_timestamp_is_refused():
    """A local time with no offset is ambiguous; the store records UTC."""
    with pytest.raises(TimeIntervalError):
        source_time.check_interval('2026-06-22T15:00:00',
                                   '2026-06-22T16:00:00Z')


def test_an_offset_timestamp_is_canonicalised_to_utc():
    """Two Items over the same interval must carry the same string."""
    start, end = source_time.check_interval('2026-06-22T09:22:29-04:00',
                                            '2026-06-22T15:00:00Z')
    assert start == '2026-06-22T13:22:29Z'
    assert end == '2026-06-22T15:00:00Z'


def test_a_yaml_timestamp_is_accepted():
    """A YAML reader hands over a datetime, not the written string."""
    from datetime import datetime, timezone
    start, end = source_time.check_interval(
        datetime(2026, 6, 22, 13, 22, 29, tzinfo=timezone.utc),
        '2026-06-22T15:00:00Z')
    assert start == '2026-06-22T13:22:29Z'


def test_a_yaml_date_is_refused():
    """A day is not an instant, and midnight UTC would be an assumption."""
    from datetime import date
    with pytest.raises(TimeIntervalError) as caught:
        source_time.check_interval(date(2026, 6, 22), '2026-06-22T15:00:00Z')
    assert 'is a date, not a time' in str(caught.value)
