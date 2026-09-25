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
The ``mws_*`` command lines, end to end over temp directories.

Every CLI is a thin shell over the library, so these check the shell: that the
arguments mean what the help says, that a store root is never guessed, and
that nothing is written under a dry run.
"""

import json

from marine_world_store import layout, source_identity
from marine_world_store.cli import (
    mws_import_source, mws_link_depth_subset, mws_regenerate_catalog,
    mws_write_revision,
)
from marine_world_store.source_time import TimeIntervalError
from marine_world_store.store_root import ENV_VAR
import pytest


@pytest.fixture
def store_root(tmp_path, monkeypatch):
    """Point the environment at a store root no real one can reach."""
    root = tmp_path / 'store'
    monkeypatch.setenv(ENV_VAR, str(root))
    return root


#: One real recording's numbers, as rosbag2 writes them.
START_NS = 1781451538661123281
DURATION_NS = 68947054668


def make_bag(directory, start_ns=START_NS, dated=True):
    """
    Build a bag-shaped directory with one mcap split.

    Dated by default, because an undated bag is now a refusal: the Items the
    tools write carry the interval this metadata records.
    """
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'rosbag2_0.mcap').write_bytes(b'ping')
    body = 'rosbag2_bagfile_information:\n  version: 9\n'
    if dated:
        body += (
            f'  duration:\n    nanoseconds: {DURATION_NS}\n'
            f'  starting_time:\n'
            f'    nanoseconds_since_epoch: {start_ns}\n'
            f'  message_count: 10555\n')
    (directory / 'metadata.yaml').write_text(body)
    return directory


def test_import_source_dry_run_writes_nothing(tmp_path, store_root, capsys):
    """It prints the id it computed and stops."""
    bag = make_bag(tmp_path / 'bag')
    assert mws_import_source.main([str(bag), '--dry-run']) == 0
    assert source_identity.bag_source_id(bag) in capsys.readouterr().out
    assert not store_root.exists()


def test_import_source_writes_the_source_item(tmp_path, store_root):
    """The Item lands under ``sources/``, named by the content id."""
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    bag = make_bag(tmp_path / 'bag')
    assert mws_import_source.main(
        [str(bag), '--platform', 'bizzyboat']) == 0
    identifier = source_identity.bag_source_id(bag)
    path = layout.sources_dir(store_root) / f'{identifier}.json'
    properties = json.loads(path.read_text())['properties']
    assert properties['mws:platform'] == 'bizzyboat'
    assert properties['start_datetime'] == '2026-06-14T15:38:58.661123Z'
    assert properties['end_datetime'] == '2026-06-14T15:40:07.608178Z'
    assert properties['datetime'] is None


def test_import_source_records_an_absolute_href_for_a_relative_path(
        tmp_path, store_root, monkeypatch):
    """
    A relative invocation path is recorded absolute.

    Regression: it was stored verbatim, and a relative href inside
    ``<root>/sources/`` resolves against that directory, not the cwd.
    """
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    bag = make_bag(tmp_path / 'work' / 'bag')
    monkeypatch.chdir(tmp_path / 'work')
    assert mws_import_source.main(['bag']) == 0
    identifier = source_identity.bag_source_id(bag)
    path = layout.sources_dir(store_root) / f'{identifier}.json'
    href = json.loads(path.read_text())['assets']['source']['href']
    assert href == str(bag)


def test_import_source_refuses_an_undated_bag(tmp_path, store_root):
    """A source nobody can date is a provenance defect; nothing is written."""
    bag = make_bag(tmp_path / 'bag', dated=False)
    with pytest.raises(TimeIntervalError) as caught:
        mws_import_source.main([str(bag)])
    assert '--start/--end' in str(caught.value)
    assert not store_root.exists()


def test_import_source_takes_a_stated_interval_when_the_bag_has_none(
        tmp_path, store_root):
    """The override for material that has a time but does not record it."""
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    bag = make_bag(tmp_path / 'bag', dated=False)
    assert mws_import_source.main([
        str(bag), '--start', '2026-06-22T13:22:29Z',
        '--end', '2026-06-22T15:00:00Z']) == 0
    identifier = source_identity.bag_source_id(bag)
    written = layout.sources_dir(store_root) / f'{identifier}.json'
    assert json.loads(written.read_text())['properties']['end_datetime'] == \
        '2026-06-22T15:00:00Z'


def test_import_source_refuses_half_a_stated_interval(tmp_path, store_root):
    """One end of a range is not a range, and STAC has no shape for it."""
    bag = make_bag(tmp_path / 'bag', dated=False)
    with pytest.raises(TimeIntervalError):
        mws_import_source.main([str(bag), '--start', '2026-06-22T13:22:29Z'])


def test_import_source_reports_a_missing_path(tmp_path, store_root):
    """A path that is not there is an error, not an empty id."""
    with pytest.raises(FileNotFoundError):
        mws_import_source.main([str(tmp_path / 'nope')])


def test_link_subset_counts_a_repeated_source_once(tmp_path, capsys):
    """
    One bag named twice is one input, fingerprinted and written once.

    Regression: a source on the command line and in the manifest (or twice in
    a manifest) was fingerprinted twice and its Item written twice.
    """
    bag = make_bag(tmp_path / 'bag')
    same_content = make_bag(tmp_path / 'copy-of-bag')
    ids, items, intervals = mws_link_depth_subset.resolve_sources(
        [{'path': str(bag)}, {'path': str(bag)},
         {'path': str(same_content)}])
    assert ids == [source_identity.bag_source_id(bag)]
    assert len(items) == 1 and len(intervals) == 1
    assert 'counted once' in capsys.readouterr().out


def test_link_subset_records_an_absolute_href_for_a_relative_path(
        tmp_path, monkeypatch):
    """A relative source path is recorded absolute, as the import does."""
    bag = make_bag(tmp_path / 'work' / 'bag')
    monkeypatch.chdir(tmp_path / 'work')
    _, items, _ = mws_link_depth_subset.resolve_sources([{'path': 'bag'}])
    assert items[0]['assets']['source']['href'] == str(bag)


def test_link_subset_global_interval_never_overrides_a_recorded_one(
        tmp_path):
    """
    The stated interval is a fallback for undated sources, not an override.

    Regression: a manifest-level start:/end: replaced the interval every bag
    recorded in its metadata.yaml.
    """
    dated = make_bag(tmp_path / 'dated')
    undated = make_bag(tmp_path / 'undated', start_ns=START_NS + 1,
                       dated=False)
    (undated / 'rosbag2_0.mcap').write_bytes(b'other')
    _, _, intervals = mws_link_depth_subset.resolve_sources(
        [{'path': str(dated)}, {'path': str(undated)}],
        '2026-06-22T13:22:29Z', '2026-06-22T15:00:00Z')
    assert intervals[0] == ('2026-06-14T15:38:58.661123Z',
                            '2026-06-14T15:40:07.608178Z')
    assert intervals[1] == ('2026-06-22T13:22:29Z', '2026-06-22T15:00:00Z')


def test_link_subset_refuses_half_an_entry_interval(tmp_path):
    """An entry's start is never stitched to the global end."""
    bag = make_bag(tmp_path / 'bag', dated=False)
    with pytest.raises(TimeIntervalError):
        mws_link_depth_subset.resolve_sources(
            [{'path': str(bag), 'start': '2026-06-22T13:22:29Z'}],
            '2026-06-01T00:00:00Z', '2026-06-30T00:00:00Z')


def test_link_subset_an_entry_statement_wins_over_the_record(tmp_path):
    """A per-source statement is about that source; it is taken as given."""
    bag = make_bag(tmp_path / 'bag')
    _, _, intervals = mws_link_depth_subset.resolve_sources(
        [{'path': str(bag), 'start': '2026-06-22T13:22:29Z',
          'end': '2026-06-22T15:00:00Z'}])
    assert intervals == [('2026-06-22T13:22:29Z', '2026-06-22T15:00:00Z')]


def test_link_subset_an_undated_source_with_no_statement_is_refused(
        tmp_path):
    """Nothing to read and nothing stated: a provenance defect."""
    bag = make_bag(tmp_path / 'bag', dated=False)
    with pytest.raises(TimeIntervalError):
        mws_link_depth_subset.resolve_sources([{'path': str(bag)}])


def test_write_revision_round_trip(tmp_path, store_root):
    """A YAML description becomes an append-only record."""
    description = tmp_path / 'datum.yaml'
    description.write_text(
        'kind: datum\n'
        'applies_to:\n  source_id: abc123\n'
        'parameters:\n  height_offset_m: 1.19\n'
        'evidence: PROJ transformation measured 2026-09-16\n'
        'reviewer: Roland Arsenault\n'
        'valid_from: 2026-06-22T00:00:00Z\n')
    assert mws_write_revision.main([str(description)]) == 0
    written = list(layout.revisions_dir(store_root).glob('*.json'))
    assert len(written) == 1
    body = json.loads(written[0].read_text())
    assert body['properties']['mws:revision']['kind'] == 'datum'


def test_write_revision_refuses_an_unknown_field(tmp_path, store_root):
    """A field nobody reads would silently not be part of the record."""
    description = tmp_path / 'datum.yaml'
    description.write_text(
        'kind: datum\napplies_to:\n  source_id: a\nparameters:\n  x: 1\n'
        'evidence: e\nreviewer: r\nvalid_from: 2026-06-22T00:00:00Z\n'
        'reviewd_by: typo\n')
    with pytest.raises(ValueError):
        mws_write_revision.main([str(description)])


def test_write_revision_dry_run_writes_nothing(tmp_path, store_root):
    """Prints the record it would append."""
    description = tmp_path / 'datum.json'
    description.write_text(json.dumps({
        'kind': 'datum', 'applies_to': {'source_id': 'a'},
        'parameters': {'x': 1}, 'evidence': 'e', 'reviewer': 'r',
        'valid_from': '2026-06-22T00:00:00Z'}))
    assert mws_write_revision.main([str(description), '--dry-run']) == 0
    assert not store_root.exists()


def test_regenerate_catalog_on_an_empty_store_is_not_an_error(
        store_root, capsys):
    """A store with no products yet is a legitimate state."""
    store_root.mkdir(parents=True)
    assert mws_regenerate_catalog.main([]) == 0
    assert 'nothing to regenerate' in capsys.readouterr().out


def test_regenerate_catalog_rebuilds_present_cells(store_root):
    """Only the cells that exist; only the Collections that changed."""
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    from marine_world_store import stac_catalog
    from test_item_schema import a_tile_item
    directory = layout.quantity_dir(
        store_root, layout.Quantity.DEPTHS, layout.State.REVIEWED,
        layout.Origin.SURVEYED)
    item = a_tile_item()
    stac_catalog.write_items(directory, [item])
    # The native tile the Item records: an Item without it is refused.
    (directory / item['assets']['data']['href']).write_bytes(b'tile')
    assert mws_regenerate_catalog.main([]) == 0
    assert layout.collection_path(directory).is_file()


def test_store_root_argument_beats_the_environment(tmp_path, store_root,
                                                   capsys):
    """Precedence is the same in every tool, because it is one function."""
    bag = make_bag(tmp_path / 'bag')
    other = tmp_path / 'other'
    mws_import_source.main([str(bag), '--store-root', str(other), '--dry-run'])
    captured = capsys.readouterr().out
    assert str(store_root) not in captured


def test_run_turns_an_expected_failure_into_a_message(capsys):
    """A CLI's normal "no" needs no traceback."""
    from marine_world_store.cli._common import run

    def boom(argv):
        raise ValueError('that is not a store root')

    assert run(boom, []) == 1
    assert 'that is not a store root' in capsys.readouterr().err


def test_run_lets_an_unexpected_exception_through():
    """An unexpected exception is a bug report, and keeps its traceback."""
    from marine_world_store.cli._common import run

    def boom(argv):
        raise KeyboardInterrupt

    with pytest.raises(KeyboardInterrupt):
        run(boom, [])
