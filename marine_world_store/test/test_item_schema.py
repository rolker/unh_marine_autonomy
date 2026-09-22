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

"""What every Item promises a consumer (design draft Part 2)."""

from marine_world_store import item_schema
from marine_world_store.item_schema import (
    build_collection, build_tile_item, CONTRACT_FIELDS, ItemSchemaError,
    validate_contract,
)
from marine_world_store.layout import Origin, Quantity, State
import pytest

GEOMETRY = {'type': 'Polygon', 'coordinates': [[
    [-70.0, 42.0], [-69.9, 42.0], [-69.9, 42.1], [-70.0, 42.1],
    [-70.0, 42.0]]]}
BBOX = [-70.0, 42.0, -69.9, 42.1]


def a_tile_item(**overrides):
    """Build a minimal valid tile Item."""
    kwargs = {
        'quantity': Quantity.DEPTHS, 'state': State.REVIEWED,
        'origin': Origin.SURVEYED, 'level': 12, 'row': 3, 'col': 4,
        'asset_href': './12_3_4.tif',
        'fingerprint_inputs': {
            'source_ids': ['abc'], 'builder_version': 'v1'},
        'uncertainty_basis': 'per-cell 1 sigma',
        'resolution_m': 2.0,
        'cell_fields': item_schema.depth_cell_fields(),
        'geometry': GEOMETRY, 'bbox': BBOX,
        'start_datetime': '2026-06-22T13:22:29Z',
        'end_datetime': '2026-06-22T15:00:00Z'}
    kwargs.update(overrides)
    return build_tile_item(**kwargs)


def a_source_item(**overrides):
    """Build a minimal valid source Item: dated, like every Item."""
    kwargs = {
        'source_id': 'abc123', 'kind': 'bag', 'name': 'a-bag',
        'start_datetime': '2026-06-22T13:22:29Z',
        'end_datetime': '2026-06-22T15:00:00Z'}
    kwargs.update(overrides)
    return item_schema.build_source_item(**kwargs)


def test_contract_fields_are_present():
    """Part 2 line 2, field by field."""
    properties = a_tile_item()['properties']
    for name in item_schema.REQUIRED_CONTRACT_FIELDS:
        assert CONTRACT_FIELDS[name] in properties, name


def test_frame_is_specific_never_bare_4326():
    """Design section 4: a specific EPSG code and the epoch, always."""
    frame = a_tile_item()['properties'][CONTRACT_FIELDS['frame']]
    assert frame['epsg'] == 9989
    assert frame['coordinate_epoch'] == 2020.0
    assert frame['name'] == 'ITRF2020'


def test_frame_can_be_overridden_for_an_untransformed_product():
    """An adapter declares the frame it holds, not the one it wishes it had."""
    frame = {'name': 'WGS84 as written', 'epsg': 4326}
    item = a_tile_item(frame=frame)
    assert item['properties'][CONTRACT_FIELDS['frame']]['epsg'] == 4326


def test_fingerprint_and_its_inputs_are_both_written():
    """A consumer that disagrees can see which input it disagrees about."""
    properties = a_tile_item()['properties']
    assert properties[CONTRACT_FIELDS['inputs']]['source_ids'] == ['abc']
    assert len(properties[CONTRACT_FIELDS['fingerprint']]) == 64


def test_geometric_error_is_carried_when_the_producer_recorded_one():
    """uma-ADR-0013 D2: the producer bakes it, the Item carries it."""
    item = a_tile_item(geometric_error_m=2.5)
    assert item['properties'][CONTRACT_FIELDS['geometric_error_m']] == 2.5


def test_an_absent_geometric_error_is_absent_not_zero():
    """Zero would claim a perfect tile to the D7 selection core."""
    assert CONTRACT_FIELDS['geometric_error_m'] not in \
        a_tile_item()['properties']


def test_sigma_fold_hook_exists_for_the_overview_writer():
    """Group B writes the rule's name; the rule itself is still open."""
    item = a_tile_item(sigma_fold='undecided')
    assert item['properties'][CONTRACT_FIELDS['sigma_fold']] == 'undecided'


def test_cell_fields_describe_only_what_the_tile_holds():
    """Contributor and measured-vs-interpolated are not in a 2-band tile."""
    fields = a_tile_item()['properties'][CONTRACT_FIELDS['cell_fields']]
    assert [f['name'] for f in fields] == ['value', 'sigma']
    with pytest.raises(ItemSchemaError):
        item_schema.depth_cell_fields(bands=4)


def test_ids_are_stable_and_say_where_the_tile_lives():
    """Two runs over the same tile write the same Item id."""
    assert a_tile_item()['id'] == 'depths-reviewed-surveyed-12_3_4'
    assert a_tile_item()['collection'] == 'depths-reviewed-surveyed'


def test_a_bbox_without_a_geometry_is_refused():
    """That is not a STAC Item, and a reader would not know what it means."""
    with pytest.raises(ItemSchemaError):
        a_tile_item(geometry=None, bbox=BBOX)


def test_time_range_is_written_as_a_range():
    """Part 2 line 2 promises a time range, not a single instant."""
    properties = a_tile_item()['properties']
    assert properties['start_datetime'] == '2026-06-22T13:22:29Z'
    assert properties['end_datetime'] == '2026-06-22T15:00:00Z'
    assert properties['datetime'] is None


def test_an_undated_item_is_refused(tmp_path):
    """
    STAC has no shape for "the time is unknown".

    An Item may carry a datetime, or a null one with both ends of a range.
    A product with neither cannot be found by the time search Part 2 line 1
    promises, so it is a provenance defect rather than a thin record, and
    nothing is written.
    """
    with pytest.raises(ItemSchemaError) as caught:
        a_tile_item(start_datetime=None, end_datetime=None)
    assert 'observation interval' in str(caught.value)
    with pytest.raises(ItemSchemaError):
        a_tile_item(end_datetime=None)
    with pytest.raises(ItemSchemaError):
        a_source_item(start_datetime=None, end_datetime=None)


def test_an_item_that_lost_its_interval_fails_validation():
    """The same rule on a hand-built Item, not only on the builders'."""
    item = a_tile_item()
    item['properties'].pop('start_datetime')
    with pytest.raises(ItemSchemaError):
        validate_contract(item)


def test_license_is_machine_readable():
    """Part 2 line 2: a licence, machine-readable, CC0 where public."""
    assert a_tile_item()['properties']['license'] == 'CC0-1.0'


def test_missing_contract_fields_are_all_named_at_once():
    """A writer should learn the schema in one round trip, not five."""
    item = a_tile_item()
    for name in ('uncertainty_basis', 'inputs'):
        item['properties'].pop(CONTRACT_FIELDS[name])
    with pytest.raises(ItemSchemaError) as caught:
        validate_contract(item)
    assert 'uncertainty_basis' in str(caught.value)
    assert 'inputs' in str(caught.value)


def test_unknown_resolution_is_null_not_absent():
    """A producer that does not know says so; the key is still there."""
    item = a_tile_item(resolution_m=None)
    assert item['properties'][CONTRACT_FIELDS['resolution_m']] is None
    validate_contract(item)


def test_collection_summarizes_its_items():
    """Part 2 line 1: enumeration, never globbing a directory."""
    items = [a_tile_item(), a_tile_item(row=4)]
    collection = build_collection(
        quantity=Quantity.DEPTHS, state=State.REVIEWED,
        origin=Origin.SURVEYED, description='test', items=items)
    assert collection['id'] == 'depths-reviewed-surveyed'
    assert collection['extent']['spatial']['bbox'] == [BBOX]
    assert collection['summaries'][CONTRACT_FIELDS['levels']] == [12]


def test_an_empty_collection_declares_an_honest_extent():
    """Unknown is the whole world; a zero-size box would be a false claim."""
    collection = build_collection(
        quantity=Quantity.DEPTHS, state=State.DRAFT, origin=Origin.SURVEYED,
        description='empty', items=[])
    assert collection['extent']['spatial']['bbox'] == [[-180, -90, 180, 90]]


def test_source_item_keeps_identity_and_lookup_metadata_apart():
    """Design section 3: platform is lookup metadata, never identity."""
    item = a_source_item(
        source_id='abc123', kind='bag', name='2026-06-22T13-22-29+00-00',
        file_keys=['rosbag2_0.mcap\tSHA256E-s1--ff'], platform='bizzyboat')
    assert item['id'] == 'abc123'
    assert item['properties'][CONTRACT_FIELDS['source']]['id'] == 'abc123'
    assert item['properties']['mws:platform'] == 'bizzyboat'


def test_engineering_material_is_labelled_as_such():
    """Design section 2: indexed beside the sources, never an input."""
    item = a_source_item(
        source_id='abc', kind='all', name='m3.all', role='engineering')
    assert item['properties'][CONTRACT_FIELDS['source']]['role'] == \
        'engineering'
    with pytest.raises(ItemSchemaError):
        a_source_item(source_id='abc', kind='all', name='m3.all',
                      role='input')
