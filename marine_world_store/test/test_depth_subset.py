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
The adapter: byte-identical tiles, real Items, nothing claimed falsely.

The fixtures are small georeferenced 2-band Float64 GeoTIFFs -- the shape
``marine_bathymetry_store`` writes -- built with GDAL in a temp directory. No
test reads the real store, and none writes anywhere but ``tmp_path``.
"""

import json

import pytest

# Guarded by hand rather than with a module-level ``pytest.importorskip``,
# which aborts the whole session under some plugin sets instead of skipping
# this module.
try:
    from osgeo import gdal
    gdal.UseExceptions()
    HAVE_GDAL = True
except ImportError:  # pragma: no cover - depends on the host
    HAVE_GDAL = False

pytestmark = pytest.mark.skipif(
    not HAVE_GDAL,
    reason='python3-gdal is a declared dependency; footprints come from the '
           'raster itself rather than from a second GGGS implementation.')

from marine_world_store import coverage, depth_subset, item_schema  # noqa: E402,I100
from marine_world_store.depth_subset import AdapterError  # noqa: E402
from marine_world_store.item_schema import CONTRACT_FIELDS  # noqa: E402
from marine_world_store.layout import Origin, Quantity, State  # noqa: E402


def make_tile(directory, level=12, row=3, col=4, value=-8.5):
    """Write a georeferenced 2-band Float64 GeoTIFF, as the store does."""
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f'{level}_{row}_{col}.tif'
    driver = gdal.GetDriverByName('GTiff')
    dataset = driver.Create(str(path), 4, 4, 2, gdal.GDT_Float64)
    dataset.SetGeoTransform((-70.0, 0.001, 0.0, 42.1, 0.0, -0.001))
    dataset.GetRasterBand(1).Fill(value)
    dataset.GetRasterBand(2).Fill(0.2)
    dataset.FlushCache()
    dataset = None
    return path


def write_coverage(directory, entries):
    """Write a coverage-manifest/1 document as the C++ writer emits it."""
    levels = {}
    for (level, row, col), error in entries.items():
        run = {'row': row, 'col_min': col, 'col_max': col}
        if error is not None:
            run['geometric_error_m'] = error
        levels.setdefault(level, []).append(run)
    (directory / 'coverage.json').write_text(json.dumps({
        'schema': 'coverage-manifest/1', 'kind': 'native',
        'levels': [{'level': lvl, 'runs': runs}
                   for lvl, runs in sorted(levels.items())]}))


@pytest.fixture
def source_layer(tmp_path):
    """Build an existing store's ``processed/`` layer with two tiles."""
    layer = tmp_path / 'existing' / 'processed'
    make_tile(layer, row=3, col=4)
    make_tile(layer, row=3, col=5)
    write_coverage(layer, {(12, 3, 4): 2.5, (12, 3, 5): None})
    return layer


#: The interval the adapted tiles' source bag recorded over. Every Item is
#: dated, and an adapter has no time of its own: this is what the CLI reads
#: out of the bag's metadata.yaml and hands over.
INTERVAL = ('2026-06-22T13:22:29Z', '2026-06-22T15:00:00Z')


def adapt(source_layer, root, **overrides):
    """Run the adapter with the arguments every test shares."""
    kwargs = {
        'source_layer_dir': source_layer, 'root': root,
        'source_ids': ['abc123'], 'builder_version': 'test/1',
        'start_datetime': INTERVAL[0], 'end_datetime': INTERVAL[1]}
    kwargs.update(overrides)
    return depth_subset.adapt_depth_tiles(**kwargs)


def test_tiles_land_in_the_rev3_tree(source_layer, tmp_path):
    """``<root>/depths/reviewed/surveyed/``, beside nothing it replaces."""
    root = tmp_path / 'rev3'
    report = adapt(source_layer, root)
    assert report.destination == root / 'depths' / 'reviewed' / 'surveyed'
    assert report.tiles_seen == 2
    assert report.tiles_copied == 2
    assert (report.destination / '12_3_4.tif').is_file()


def test_the_copy_is_byte_identical(source_layer, tmp_path):
    """The one promise the adapter makes about the pixels."""
    report = adapt(source_layer, tmp_path / 'rev3')
    for name in ('12_3_4.tif', '12_3_5.tif'):
        assert depth_subset.file_sha256(source_layer / name) == \
            depth_subset.file_sha256(report.destination / name)


def test_the_existing_store_is_untouched(source_layer, tmp_path):
    """It is production data read by CAMP and the costmap."""
    before = {p.name: depth_subset.file_sha256(p)
              for p in source_layer.iterdir() if p.is_file()}
    adapt(source_layer, tmp_path / 'rev3')
    after = {p.name: depth_subset.file_sha256(p)
             for p in source_layer.iterdir() if p.is_file()}
    assert before == after


def test_re_running_copies_nothing(source_layer, tmp_path):
    """Idempotent: an unchanged tile is not rewritten, so a replica is quiet."""
    root = tmp_path / 'rev3'
    adapt(source_layer, root)
    report = adapt(source_layer, root)
    assert report.tiles_copied == 0
    assert report.tiles_unchanged == 2


def test_geometric_error_comes_from_the_producers_manifest(
        source_layer, tmp_path):
    """uma-ADR-0013 D2/D3: read the existing convention, do not invent one."""
    report = adapt(source_layer, tmp_path / 'rev3')
    by_id = {item['id']: item for item in report.items}
    with_error = by_id['depths-reviewed-surveyed-12_3_4']['properties']
    assert with_error[CONTRACT_FIELDS['geometric_error_m']] == 2.5


def test_a_tile_with_no_recorded_error_records_none(source_layer, tmp_path):
    """Never zero: the selection core falls back, and that is visible."""
    report = adapt(source_layer, tmp_path / 'rev3')
    by_id = {item['id']: item for item in report.items}
    without = by_id['depths-reviewed-surveyed-12_3_5']['properties']
    assert CONTRACT_FIELDS['geometric_error_m'] not in without
    assert report.missing_geometric_error == 1


def test_items_declare_the_frame_the_tiles_actually_carry(
        source_layer, tmp_path):
    """A byte-identical copy has not been transformed into the store frame."""
    report = adapt(source_layer, tmp_path / 'rev3')
    frame = report.items[0]['properties'][CONTRACT_FIELDS['frame']]
    assert frame['epsg'] == 4326
    assert 'not applied' in frame['transformation_to_store_frame']
    # Regression: proj:epsg claimed the store frame (9989) regardless.
    for item in report.items:
        assert item['properties']['proj:epsg'] == 4326


def test_items_carry_the_footprint(source_layer, tmp_path):
    """Part 2 line 1: findable by extent."""
    item = adapt(source_layer, tmp_path / 'rev3').items[0]
    assert item['geometry']['type'] == 'Polygon'
    assert item['bbox'][0] == pytest.approx(-70.0)
    assert item['bbox'][3] == pytest.approx(42.1)


def test_items_fingerprint_over_the_named_sources(source_layer, tmp_path):
    """The source id is an input; the builder version is another."""
    item = adapt(source_layer, tmp_path / 'rev3',
                 source_ids=['abc123', 'def456']).items[0]
    inputs = item['properties'][CONTRACT_FIELDS['inputs']]
    assert inputs['source_ids'] == ['abc123', 'def456']
    assert inputs['builder_version'] == 'test/1'


def test_unnamed_sources_are_refused(source_layer, tmp_path):
    """A product with no recorded inputs has no fingerprint worth writing."""
    with pytest.raises(AdapterError):
        adapt(source_layer, tmp_path / 'rev3', source_ids=[])


def test_a_tile_that_cannot_be_dated_is_refused(source_layer, tmp_path):
    """An undated product could not be found by Part 2 line 1's time search."""
    root = tmp_path / 'rev3'
    with pytest.raises(AdapterError) as caught:
        adapt(source_layer, root, start_datetime=None, end_datetime=None)
    assert 'observation interval' in str(caught.value)
    assert not root.exists()


def test_half_an_interval_is_refused(source_layer, tmp_path):
    """One end of a range is not a range; STAC has no shape for it."""
    with pytest.raises(AdapterError):
        adapt(source_layer, tmp_path / 'rev3', end_datetime=None)


def test_items_carry_the_interval_they_were_given(source_layer, tmp_path):
    """The union of the source bags' intervals, as the CLI computed it."""
    properties = adapt(source_layer, tmp_path / 'rev3').items[0]['properties']
    assert properties['start_datetime'] == INTERVAL[0]
    assert properties['end_datetime'] == INTERVAL[1]
    assert properties['datetime'] is None


def test_a_level_filter_selects_a_subset(source_layer, tmp_path):
    """The subset is the point; a level nobody holds is a loud error."""
    assert adapt(source_layer, tmp_path / 'a', levels=[12]).tiles_seen == 2
    with pytest.raises(AdapterError):
        adapt(source_layer, tmp_path / 'b', levels=[9])


def test_an_empty_layer_is_refused(tmp_path):
    """Adapting nothing silently would report success over no data."""
    empty = tmp_path / 'existing' / 'processed'
    empty.mkdir(parents=True)
    with pytest.raises(AdapterError):
        adapt(empty, tmp_path / 'rev3')


def test_dry_run_writes_nothing(source_layer, tmp_path):
    """It reports what it would do and leaves the disk alone."""
    root = tmp_path / 'rev3'
    report = adapt(source_layer, root, dry_run=True)
    assert report.tiles_seen == 2
    assert report.tiles_copied == 0
    assert not report.destination.exists()


def test_a_layer_with_no_manifest_still_adapts(tmp_path):
    """The scan fallback: no manifest means no errors, not no tiles."""
    layer = tmp_path / 'existing' / 'draft'
    make_tile(layer)
    report = adapt(layer, tmp_path / 'rev3', state=State.DRAFT)
    assert report.tiles_seen == 1
    assert report.missing_geometric_error == 1
    assert coverage.load_layer_coverage(layer) is None


def test_state_and_origin_decide_the_cell(source_layer, tmp_path):
    """The two axes are directories, and the Item repeats them."""
    report = adapt(source_layer, tmp_path / 'rev3',
                   state=State.DRAFT, origin=Origin.SURVEYED)
    assert report.destination.parts[-2:] == ('draft', 'surveyed')
    properties = report.items[0]['properties']
    assert properties[CONTRACT_FIELDS['state']] == 'draft'
    assert properties[CONTRACT_FIELDS['quantity']] == Quantity.DEPTHS.value


def test_cell_fields_match_the_two_band_container(source_layer, tmp_path):
    """The Item does not claim fields the tile does not hold."""
    item = adapt(source_layer, tmp_path / 'rev3').items[0]
    names = [f['name']
             for f in item['properties'][CONTRACT_FIELDS['cell_fields']]]
    assert names == ['value', 'sigma']
    assert item_schema.depth_cell_fields()[0].units == 'm'


def test_writing_the_items_needs_pystac(source_layer, tmp_path):
    """The write half is separated so the adapter itself stays testable."""
    pytest.importorskip(
        'pystac',
        reason='python3-pystac is a declared dependency; run rosdep install.')
    report = adapt(source_layer, tmp_path / 'rev3')
    depth_subset.write_report_items(report)
    assert report.items_written == 2
    assert (report.destination / 'collection.json').is_file()
    again = adapt(source_layer, tmp_path / 'rev3')
    depth_subset.write_report_items(again)
    assert again.items_written == 0


def test_subset_manifest_sources_are_inputs_not_literals():
    """The bag paths come from a manifest or a flag; never from the code."""
    entries = depth_subset.sources_from_manifest(
        {'sources': [{'path': '/nas/logs/bag', 'platform': 'bizzyboat'}]})
    assert entries[0]['path'] == '/nas/logs/bag'
    entries = depth_subset.sources_from_manifest({'sources': ['/nas/bag']})
    assert entries[0] == {'path': '/nas/bag'}
    with pytest.raises(AdapterError):
        depth_subset.sources_from_manifest({'sources': []})


# --- the CLI: where the interval actually comes from --------------------

#: Two recordings, an hour apart, in rosbag2's own units.
FIRST_BAG_NS = 1782134549000000000
SECOND_BAG_NS = FIRST_BAG_NS + 3600 * 10 ** 9
ONE_MINUTE_NS = 60 * 10 ** 9


def make_bag(directory, start_ns, duration_ns=ONE_MINUTE_NS, dated=True):
    """Build a bag-shaped directory rosbag2 would have written."""
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'rosbag2_0.mcap').write_bytes(
        f'ping {start_ns}'.encode('utf-8'))
    body = 'rosbag2_bagfile_information:\n  version: 9\n'
    if dated:
        body += (
            f'  duration:\n    nanoseconds: {duration_ns}\n'
            f'  starting_time:\n'
            f'    nanoseconds_since_epoch: {start_ns}\n'
            f'  message_count: 42\n')
    (directory / 'metadata.yaml').write_text(body)
    return directory


def test_the_cli_dates_the_tiles_from_the_bags(source_layer, tmp_path):
    """
    The adapter's interval is read, never asked for.

    Two bags an hour apart: each source Item carries its own recording
    interval, and the tiles carry the union -- they were built from both.
    """
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    from marine_world_store import stac_catalog
    from marine_world_store.cli import mws_link_depth_subset
    from marine_world_store import layout as layout_module
    root = tmp_path / 'rev3'
    first = make_bag(tmp_path / 'bags' / 'first', FIRST_BAG_NS)
    second = make_bag(tmp_path / 'bags' / 'second', SECOND_BAG_NS)
    assert mws_link_depth_subset.main([
        '--source-store', str(source_layer.parent), '--layer', 'processed',
        '--store-root', str(root),
        '--source', str(first), '--source', str(second)]) == 0

    tiles = stac_catalog.read_items(
        layout_module.quantity_dir(root, Quantity.DEPTHS, State.REVIEWED,
                                   Origin.SURVEYED))
    assert tiles, 'the adapter wrote no tile Items'
    for item in tiles:
        assert item['properties']['start_datetime'] == \
            '2026-06-22T13:22:29Z'
        assert item['properties']['end_datetime'] == '2026-06-22T14:23:29Z'
        assert item['properties']['datetime'] is None

    sources = stac_catalog.read_items(layout_module.sources_dir(root))
    spans = sorted((i['properties']['start_datetime'],
                    i['properties']['end_datetime']) for i in sources)
    assert spans == [('2026-06-22T13:22:29Z', '2026-06-22T13:23:29Z'),
                     ('2026-06-22T14:22:29Z', '2026-06-22T14:23:29Z')]


def test_the_cli_refuses_a_tile_it_cannot_date(source_layer, tmp_path):
    """An undated bag stops the run before anything is copied."""
    from marine_world_store.cli import mws_link_depth_subset
    from marine_world_store.source_time import TimeIntervalError
    root = tmp_path / 'rev3'
    bag = make_bag(tmp_path / 'bags' / 'undated', FIRST_BAG_NS, dated=False)
    with pytest.raises(TimeIntervalError):
        mws_link_depth_subset.main([
            '--source-store', str(source_layer.parent),
            '--store-root', str(root), '--source', str(bag)])
    assert not root.exists()


def test_the_cli_takes_a_stated_interval_for_an_undated_source(
        source_layer, tmp_path):
    """The override, for material with a time it does not record."""
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    from marine_world_store.cli import mws_link_depth_subset
    root = tmp_path / 'rev3'
    bag = make_bag(tmp_path / 'bags' / 'undated', FIRST_BAG_NS, dated=False)
    assert mws_link_depth_subset.main([
        '--source-store', str(source_layer.parent),
        '--store-root', str(root), '--source', str(bag),
        '--start', '2026-06-22T13:00:00Z',
        '--end', '2026-06-22T16:00:00Z']) == 0
    assert (root / 'depths' / 'reviewed' / 'surveyed').is_dir()
