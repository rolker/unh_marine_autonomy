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


def adapt(source_layer, root, **overrides):
    """Run the adapter with the arguments every test shares."""
    kwargs = {
        'source_layer_dir': source_layer, 'root': root,
        'source_ids': ['abc123'], 'builder_version': 'test/1'}
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
