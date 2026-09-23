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
Overview tiles get Items built from their lineage, never invented.

Fixtures: small georeferenced GeoTIFFs (2-band natives, 4-band overviews) and
the per-tile records the C++ writer leaves, in a temp directory. What is
checked is that the Item's time, inputs and frame come from the children, and
that a tile whose lineage is unknown is refused rather than given an Item.
"""

import json

import pytest

try:
    from osgeo import gdal
    gdal.UseExceptions()
    HAVE_GDAL = True
except ImportError:  # pragma: no cover - depends on the host
    HAVE_GDAL = False

pytestmark = pytest.mark.skipif(
    not HAVE_GDAL, reason='python3-gdal is a declared dependency; the '
                          'footprint of an overview is read from the tile.')

from marine_world_store import item_schema, overview_items  # noqa: E402,I100
from marine_world_store.item_schema import CONTRACT_FIELDS  # noqa: E402
from marine_world_store.layout import Origin, Quantity, State  # noqa: E402

CELL = {'quantity': Quantity.DEPTHS, 'state': State.REVIEWED,
        'origin': Origin.SURVEYED}
LEGACY = {'name': 'WGS84 as written', 'epsg': 4326}


def raster(path, bands):
    """Write a tiny georeferenced Float64 GeoTIFF with ``bands`` bands."""
    path.parent.mkdir(parents=True, exist_ok=True)
    dataset = gdal.GetDriverByName('GTiff').Create(
        str(path), 4, 4, bands, gdal.GDT_Float64)
    dataset.SetGeoTransform((-70.0, 0.001, 0.0, 42.1, 0.0, -0.001))
    dataset.FlushCache()
    return path


def native(layer, name, start, end, sources, frame=None):
    """Write a native tile and its Item, as the adapter leaves them."""
    raster(layer / name, 2)
    level, row, col = (int(p) for p in name[:-4].split('_'))
    return item_schema.build_tile_item(
        **CELL, level=level, row=row, col=col, asset_href=f'./{name}',
        fingerprint_inputs={'source_ids': sources,
                            'builder_version': 'mws_link_depth_subset/1'},
        uncertainty_basis='per-cell 1 sigma', resolution_m=2.0,
        start_datetime=start, end_datetime=end, frame=frame or LEGACY)


def overview(layer, name, children, error=5.0, sigma_fold='undecided'):
    """Write a derived tile and its record, as the C++ writer leaves them."""
    tile = raster(layer / 'overviews' / name, 4)
    record = {'schema': 'depth-overview-tile/1', 'geometric_error_m': error,
              'bands': ['min', 'mean', 'count', 'sigma'],
              'sigma_fold': sigma_fold, 'sigma_band_written': False,
              'children_used': len(children or [])}
    if children is not None:
        record['children'] = children
    tile.with_suffix('.json').write_text(json.dumps(record))
    return tile


@pytest.fixture
def layer(tmp_path):
    """Two natives at 13, their parent at 12, its parent at 11."""
    layer = tmp_path / 'depths' / 'reviewed' / 'surveyed'
    items = [
        native(layer, '13_2_2.tif', '2026-06-22T13:00:00Z',
               '2026-06-22T14:00:00Z', ['bag-a']),
        native(layer, '13_2_3.tif', '2026-06-23T09:00:00Z',
               '2026-06-23T10:00:00Z', ['bag-b']),
    ]
    overview(layer, '12_1_1.tif', ['13_2_2.tif', '13_2_3.tif'], error=5.0)
    overview(layer, '11_0_0.tif', ['overviews/12_1_1.tif'], error=9.0)
    return layer, items


def test_an_overview_item_is_dated_by_its_children(layer):
    """Every Item is dated; an overview by the union of its lineage."""
    directory, natives = layer
    items = {i['id']: i['properties'] for i in
             overview_items.build_overview_items(
                 directory, **CELL, existing_items=natives)}
    for name in ('depths-reviewed-surveyed-12_1_1',
                 'depths-reviewed-surveyed-11_0_0'):
        assert items[name]['start_datetime'] == '2026-06-22T13:00:00Z'
        assert items[name]['end_datetime'] == '2026-06-23T10:00:00Z'


def test_an_overview_item_carries_what_the_contract_promises(layer):
    """geometric_error_m, sigma_fold, the 4-band fields, the lineage."""
    directory, natives = layer
    by_id = {i['id']: i for i in overview_items.build_overview_items(
        directory, **CELL, existing_items=natives)}
    item = by_id['depths-reviewed-surveyed-12_1_1']
    properties = item['properties']
    assert properties[CONTRACT_FIELDS['geometric_error_m']] == 5.0
    assert properties[CONTRACT_FIELDS['sigma_fold']] == 'undecided'
    assert [f['name'] for f in properties[CONTRACT_FIELDS['cell_fields']]] \
        == ['min', 'mean', 'count', 'sigma']
    assert properties[overview_items.CHILDREN_FIELD] == [
        '13_2_2.tif', '13_2_3.tif']
    assert properties[overview_items.OVERVIEW_FLAG] is True
    assert item['assets']['data']['href'] == './overviews/12_1_1.tif'
    # The frame is the children's; the untransformed legacy frame stays so.
    assert properties[CONTRACT_FIELDS['frame']] == LEGACY
    assert properties['proj:epsg'] == 4326
    assert properties[CONTRACT_FIELDS['inputs']]['source_ids'] == [
        'bag-a', 'bag-b']


def test_a_changed_sigma_rule_is_a_new_fingerprint(tmp_path):
    """A rule decided later changes the fingerprint -- never a migration."""
    fingerprints = []
    for rule in ('undecided', 'pooled'):
        directory = tmp_path / rule / 'depths' / 'reviewed' / 'surveyed'
        natives = [native(directory, '13_2_2.tif', '2026-06-22T13:00:00Z',
                          '2026-06-22T14:00:00Z', ['bag-a'])]
        overview(directory, '12_1_1.tif', ['13_2_2.tif'], sigma_fold=rule)
        item = overview_items.build_overview_items(
            directory, **CELL, existing_items=natives)[0]
        fingerprints.append(item['properties'][CONTRACT_FIELDS['fingerprint']])
    assert fingerprints[0] != fingerprints[1]


@pytest.mark.parametrize('children,match', [
    (None, 'no per-tile record naming its children'),
    (['13_9_9.tif'], 'no Item'),
])
def test_an_overview_with_unknown_lineage_is_refused(tmp_path, children,
                                                     match):
    """No lineage, no time: refused and named, like every undated Item."""
    directory = tmp_path / 'depths' / 'reviewed' / 'surveyed'
    natives = [native(directory, '13_2_2.tif', '2026-06-22T13:00:00Z',
                      '2026-06-22T14:00:00Z', ['bag-a'])]
    overview(directory, '12_1_1.tif', children)
    with pytest.raises(overview_items.OverviewItemError, match=match):
        overview_items.build_overview_items(
            directory, **CELL, existing_items=natives)


def test_children_in_two_frames_are_refused(tmp_path):
    """A fold over two frames has no single frame to declare."""
    directory = tmp_path / 'depths' / 'reviewed' / 'surveyed'
    natives = [
        native(directory, '13_2_2.tif', '2026-06-22T13:00:00Z',
               '2026-06-22T14:00:00Z', ['bag-a']),
        native(directory, '13_2_3.tif', '2026-06-22T13:00:00Z',
               '2026-06-22T14:00:00Z', ['bag-b'],
               frame=item_schema.store_frame()),
    ]
    overview(directory, '12_1_1.tif', ['13_2_2.tif', '13_2_3.tif'])
    with pytest.raises(overview_items.OverviewItemError, match='frame'):
        overview_items.build_overview_items(
            directory, **CELL, existing_items=natives)


def test_the_catalog_step_writes_overview_items_for_the_layer_it_is_given(
        layer, tmp_path, monkeypatch):
    """
    Catalog the layer named, not the environment's store root.

    Regression: the DAG's catalog rule ran mws_regenerate_catalog with no
    root, so it regenerated $WORLD_STORE_ROOT / the config / the default
    instead of the layer the DAG had just built.
    """
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    from marine_world_store import stac_catalog
    from marine_world_store.cli import mws_regenerate_catalog
    directory, natives = layer
    elsewhere = tmp_path / 'some-other-store'
    monkeypatch.setenv('WORLD_STORE_ROOT', str(elsewhere))
    stac_catalog.write_items(directory, natives)
    assert mws_regenerate_catalog.main(['--layer-dir', str(directory)]) == 0
    assert not elsewhere.exists()
    collection = json.loads((directory / 'collection.json').read_text())
    assert collection['summaries'][CONTRACT_FIELDS['levels']] == [11, 12, 13]
    assert (directory / 'depths-reviewed-surveyed-11_0_0.json').is_file()

    # A pruned overview tile takes its Item with it.
    (directory / 'overviews' / '11_0_0.tif').unlink()
    (directory / 'overviews' / '11_0_0.json').unlink()
    assert mws_regenerate_catalog.main(['--layer-dir', str(directory)]) == 0
    assert not (directory / 'depths-reviewed-surveyed-11_0_0.json').exists()
    collection = json.loads((directory / 'collection.json').read_text())
    assert collection['summaries'][CONTRACT_FIELDS['levels']] == [12, 13]


def test_a_pruned_items_file_is_removed_never_one_named_by_its_id(
        layer, tmp_path):
    """
    Remove the file an Item was read from; its ``id`` is data, not a path.

    Regression: the stale Item was deleted as ``<layer>/<id>.json``, so an id
    holding ``../`` deleted a file outside the layer, and an id that disagreed
    with its filename deleted the wrong file (and left the stale one).
    """
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    from marine_world_store import stac_catalog
    from marine_world_store.cli import mws_regenerate_catalog
    directory, natives = layer
    stac_catalog.write_items(directory, natives)
    assert mws_regenerate_catalog.main(['--layer-dir', str(directory)]) == 0
    stale = directory / 'depths-reviewed-surveyed-11_0_0.json'
    outside = directory.parent / 'victim.json'
    outside.write_text('{}')
    document = json.loads(stale.read_text())
    document['id'] = '../victim'
    stale.write_text(json.dumps(document))
    (directory / 'overviews' / '11_0_0.tif').unlink()
    (directory / 'overviews' / '11_0_0.json').unlink()
    assert mws_regenerate_catalog.main(['--layer-dir', str(directory)]) == 0
    assert outside.exists()
    assert not stale.exists()


def test_the_catalog_step_refuses_a_path_that_is_not_a_layer(tmp_path):
    """A typo'd layer is refused by name, not catalogued as a new cell."""
    from marine_world_store.cli import mws_regenerate_catalog
    from marine_world_store.layout import LayoutError
    bogus = tmp_path / 'depths' / 'reviwed' / 'surveyed'
    bogus.mkdir(parents=True)
    with pytest.raises(LayoutError):
        mws_regenerate_catalog.main(['--layer-dir', str(bogus)])


def test_the_index_inputs_come_from_the_items_by_kind(layer, capsys):
    """
    One list per band schema, from the Items, never from a glob.

    An Item whose tile is missing is an error: the index would point at
    nothing.
    """
    pytest.importorskip('pystac', reason='declared dependency; run rosdep')
    from marine_world_store import stac_catalog
    from marine_world_store.cli import mws_list_tiles, mws_regenerate_catalog
    directory, natives = layer
    stac_catalog.write_items(directory, natives)
    (directory / '13_9_9.tif').write_bytes(b'')   # a tile no Item records
    assert mws_regenerate_catalog.main(['--layer-dir', str(directory)]) == 0
    capsys.readouterr()
    assert mws_list_tiles.main([str(directory), '--kind', 'native']) == 0
    assert capsys.readouterr().out.split() == [
        str(directory / '13_2_2.tif'), str(directory / '13_2_3.tif')]
    assert mws_list_tiles.main(
        [str(directory), '--kind', 'overview', '--optfile']) == 0
    assert capsys.readouterr().out.split() == [
        f'"{directory / "overviews" / "11_0_0.tif"}"',
        f'"{directory / "overviews" / "12_1_1.tif"}"']
    (directory / '13_2_2.tif').unlink()
    with pytest.raises(OSError, match='not on disk'):
        mws_list_tiles.main([str(directory), '--kind', 'native'])
