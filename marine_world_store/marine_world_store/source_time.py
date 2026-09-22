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

r"""
The observation interval an Item is dated by (design draft Part 2 line 2).

Every Item this package writes carries ``start_datetime``/``end_datetime``:
the interval the material it is made of was observed over. The interval is
**derived from the sources**, not supplied by a human and not invented --
a bag directory's ``metadata.yaml`` records the recording's start and its
duration, and rosbag2 writes it for every bag.

``metadata.yaml`` is deliberately outside the Merkle source id (see
:mod:`marine_world_store.source_identity`: ``ros2 bag reindex`` rewrites it
and must not mint a new source), which is exactly why it can be *read* here:
the identity is the sensor data, the time is lookup metadata about it.

An Item with no derivable interval is a **provenance defect**, not a gap to be
papered over: STAC gives an Item two legal shapes -- one ``datetime``, or a
null ``datetime`` with both ends of a range -- and no third shape for
"unknown". A product nobody can date cannot be searched by time, which Part 2
line 1 promises, so the writers raise :class:`TimeIntervalError` and write
nothing rather than record a false or absent time.

Times are RFC 3339 in UTC with a ``Z`` suffix, at microsecond resolution:
rosbag2 records nanoseconds, Python's :class:`~datetime.datetime` holds
microseconds. A start is truncated and an end rounded **up** to the next
microsecond, so the recorded interval always covers the data rather than
ending a few hundred nanoseconds before it.
"""

from __future__ import annotations

from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping, Optional, Sequence, Tuple, Union

PathLike = Union[str, Path]

#: The file a bag directory records its times in. Excluded from the source
#: id (identity is the sensor data), read here for the time.
BAG_METADATA_FILENAME = 'metadata.yaml'

#: rosbag2's top-level key in that file.
_BAG_INFO_KEY = 'rosbag2_bagfile_information'

_NS_PER_SECOND = 1_000_000_000
_NS_PER_MICROSECOND = 1_000

#: How a source with no readable time can still be dated: the operator states
#: the interval. Named in every refusal so the message carries its own remedy.
OVERRIDE_HINT = (
    'pass the interval explicitly (--start/--end, or start:/end: in the '
    'subset manifest) if you know it from outside the bag')


class TimeIntervalError(ValueError):
    """A source or product whose observation interval cannot be determined."""


def format_rfc3339(moment: datetime) -> str:
    """Spell ``moment`` the way an Item carries it: UTC, ``Z``-suffixed."""
    moment = moment.astimezone(timezone.utc)
    text = moment.strftime('%Y-%m-%dT%H:%M:%S')
    if moment.microsecond:
        text += f'.{moment.microsecond:06d}'
    return text + 'Z'


def parse_rfc3339(text: str, *, what: str = 'time') -> datetime:
    """
    Parse an RFC 3339 timestamp, ``Z`` included.

    :raises TimeIntervalError: on anything that is not one. A timestamp the
        store cannot parse is a timestamp it cannot compare, and an Item
        carrying it could not be found by a time search.
    """
    if not isinstance(text, str) or not text.strip():
        raise TimeIntervalError(f'{what} is empty; expected an RFC 3339 time')
    candidate = text.strip()
    try:
        moment = datetime.fromisoformat(candidate.replace('Z', '+00:00'))
    except ValueError as exc:
        raise TimeIntervalError(
            f'{what} {text!r} is not an RFC 3339 timestamp: {exc}') from exc
    if moment.tzinfo is None:
        raise TimeIntervalError(
            f'{what} {text!r} has no time zone; the store records UTC, and a '
            'local time without its offset is ambiguous')
    return moment


def as_rfc3339(value: Any, *, what: str = 'time') -> str:
    """
    Spell ``value`` canonically, whatever a reader handed over.

    YAML is the reason this exists: ``start: 2026-06-22T13:22:29Z`` in a
    manifest arrives as a :class:`~datetime.datetime`, not a string, and a
    bare ``2026-06-22`` arrives as a :class:`~datetime.date`. A date is
    **not** an interval endpoint -- it names a day, in no particular time
    zone -- so it is refused rather than assumed to mean midnight UTC.

    :raises TimeIntervalError: on a date, a naive datetime, or anything that
        is not an RFC 3339 time.
    """
    if isinstance(value, datetime):
        if value.tzinfo is None:
            raise TimeIntervalError(
                f'{what} {value!r} has no time zone; the store records UTC')
        return format_rfc3339(value)
    if isinstance(value, date):
        raise TimeIntervalError(
            f'{what} {value!r} is a date, not a time: quote it in the '
            'manifest ("2026-06-22T13:22:29Z") so it names an instant')
    return format_rfc3339(parse_rfc3339(value, what=what))


def check_interval(start: Any, end: Any, *,
                   what: str = 'this Item') -> Tuple[str, str]:
    """
    Check an interval is present, parseable and ordered.

    :returns: the two timestamps, canonically spelled (UTC, ``Z``-suffixed),
        so that two Items covering the same interval carry the same strings
        and a fingerprint over them is stable.
    :raises TimeIntervalError: when either end is missing, unparseable, or the
        end precedes the start.
    """
    missing = [name for name, value in (('start', start), ('end', end))
               if not value]
    if missing:
        raise TimeIntervalError(
            f'{what} has no observation interval ({", ".join(missing)} '
            f'missing). Every Item is dated from its sources; '
            f'{OVERRIDE_HINT}.')
    first = as_rfc3339(start, what=f'{what} start')
    last = as_rfc3339(end, what=f'{what} end')
    if parse_rfc3339(last) < parse_rfc3339(first):
        raise TimeIntervalError(
            f'{what} ends ({last}) before it starts ({first})')
    return first, last


def interval_from_nanoseconds(start_ns: int, duration_ns: int
                              ) -> Tuple[str, str]:
    """
    Build an interval from rosbag2's own numbers.

    The end is rounded **up** to the next microsecond when the duration does
    not land on one, so the interval covers the last message rather than
    ending just before it.
    """
    if start_ns <= 0:
        raise TimeIntervalError(
            f'recorded start time is {start_ns} ns since the epoch, which is '
            'not a time anything was observed at')
    if duration_ns < 0:
        raise TimeIntervalError(
            f'recorded duration is negative ({duration_ns} ns)')
    epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
    start = epoch + timedelta(microseconds=start_ns // _NS_PER_MICROSECOND)
    end_ns = start_ns + duration_ns
    end_us = -(-end_ns // _NS_PER_MICROSECOND)  # ceiling division
    end = epoch + timedelta(microseconds=end_us)
    return format_rfc3339(start), format_rfc3339(end)


def read_bag_metadata(bag_dir: PathLike) -> Mapping[str, Any]:
    """
    Read a bag directory's ``metadata.yaml``.

    :raises TimeIntervalError: when the file is absent, unreadable, not YAML,
        or does not carry rosbag2's own top-level key -- each named
        separately, because "the bag has no metadata" and "the metadata is not
        a rosbag2 document" call for different repairs.
    """
    path = Path(bag_dir) / BAG_METADATA_FILENAME
    if not path.is_file():
        raise TimeIntervalError(
            f'{path}: no {BAG_METADATA_FILENAME}, so the bag cannot be dated. '
            f'`ros2 bag reindex` rewrites it without changing the source id; '
            f'{OVERRIDE_HINT}.')
    import yaml
    try:
        document = yaml.safe_load(path.read_text())
    except (OSError, yaml.YAMLError) as exc:
        raise TimeIntervalError(f'{path}: cannot be read as YAML: {exc}') \
            from exc
    if not isinstance(document, Mapping) or _BAG_INFO_KEY not in document:
        raise TimeIntervalError(
            f'{path}: not a rosbag2 metadata document (no {_BAG_INFO_KEY}:)')
    info = document[_BAG_INFO_KEY]
    if not isinstance(info, Mapping):
        raise TimeIntervalError(
            f'{path}: {_BAG_INFO_KEY} is not a mapping')
    return info


def _nanoseconds(container: Any, key: str) -> Optional[int]:
    """Read one of rosbag2's ``{key: {<unit>: <int>}}`` time fields."""
    if not isinstance(container, Mapping):
        return None
    value = container.get(key)
    if isinstance(value, Mapping):
        for unit in ('nanoseconds_since_epoch', 'nanoseconds'):
            if isinstance(value.get(unit), int):
                return int(value[unit])
        return None
    return int(value) if isinstance(value, int) else None


def bag_interval(bag_dir: PathLike) -> Tuple[str, str]:
    """
    The interval a bag directory recorded over, from its ``metadata.yaml``.

    Two readings, in order: the bag's own ``starting_time`` plus ``duration``,
    and -- when those are absent or the duration is missing -- the union of
    the per-split ``files:`` entries, which is the message time range the same
    file records split by split.

    :raises TimeIntervalError: when neither reading yields an interval. An
        empty bag (no messages) is refused too: a recording that observed
        nothing has no observation interval, and dating a product by one would
        be a claim about data that is not there.
    """
    info = read_bag_metadata(bag_dir)
    if info.get('message_count') == 0:
        raise TimeIntervalError(
            f'{bag_dir}: the bag records 0 messages, so it has no observation '
            'interval')
    start_ns = _nanoseconds(info, 'starting_time')
    duration_ns = _nanoseconds(info, 'duration')
    if start_ns is not None and duration_ns is not None:
        return interval_from_nanoseconds(start_ns, duration_ns)

    spans = []
    for entry in info.get('files') or []:
        entry_start = _nanoseconds(entry, 'starting_time')
        entry_duration = _nanoseconds(entry, 'duration')
        if entry_start is None:
            continue
        spans.append(interval_from_nanoseconds(entry_start,
                                               entry_duration or 0))
    if spans:
        return union_intervals(spans, what=str(bag_dir))
    raise TimeIntervalError(
        f'{bag_dir}: its {BAG_METADATA_FILENAME} records no usable '
        f'starting_time/duration and no dated files: entries; '
        f'{OVERRIDE_HINT}.')


def source_interval(path: PathLike) -> Tuple[str, str]:
    """
    The interval for any kind of source: a bag directory, or a single file.

    A single-file source (a cast, a prior grid) carries no rosbag2 metadata,
    so there is nothing to derive from and the caller must state the interval.
    The file's mtime is deliberately **not** used: when a file was copied is
    not when its data was observed.
    """
    path = Path(path)
    if path.is_dir():
        return bag_interval(path)
    raise TimeIntervalError(
        f'{path}: a single-file source carries no recorded interval (a file '
        f'modification time is not an observation time); {OVERRIDE_HINT}.')


def union_intervals(intervals: Iterable[Sequence[Any]], *,
                    what: str = 'this product') -> Tuple[str, str]:
    """
    The interval covering every one of ``intervals`` -- earliest to latest.

    What a product Item carries: a tile built from three bags was observed
    over all three. Every contributing interval must be complete; one source
    that cannot be dated makes the product undatable, and a union that quietly
    dropped it would claim a narrower observation window than the truth.

    :raises TimeIntervalError: on an empty input or an incomplete member.
    """
    pairs = [check_interval(start, end, what=what)
             for start, end in (tuple(i) for i in intervals)]
    if not pairs:
        raise TimeIntervalError(
            f'{what} names no dated source, so it has no observation '
            f'interval; {OVERRIDE_HINT}.')
    start = min(pairs, key=lambda p: parse_rfc3339(p[0]))[0]
    end = max(pairs, key=lambda p: parse_rfc3339(p[1]))[1]
    return start, end
