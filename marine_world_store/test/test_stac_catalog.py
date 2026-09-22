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
Writing Items and Collections, and the changed-only rewrite rule.

``pystac`` resolves through the repo-root ``rosdep.yaml`` local key and is not
installed until ``rosdep install`` runs, so these skip rather than fail on a
host that has not had it installed yet -- the schema they exercise is tested
without pystac in ``test_item_schema.py``.
"""

import json

import pytest

# A module-level ``pytest.importorskip`` aborts the whole session under some
# plugin sets rather than skipping this module, so the optional import is
# guarded by hand and the skip is a mark.
try:
    import pystac  # noqa: F401
    HAVE_PYSTAC = True
except ImportError:  # pragma: no cover - depends on the host
    HAVE_PYSTAC = False

pytestmark = pytest.mark.skipif(
    not HAVE_PYSTAC,
    reason='python3-pystac is a declared dependency (repo-root rosdep.yaml); '
           'run `rosdep install` to make these run. The Item schema itself is '
           'covered by test_item_schema.py, which needs no pystac.')

if HAVE_PYSTAC:
    from marine_world_store import layout, stac_catalog
    from marine_world_store.layout import Origin, Quantity, State
    from test_item_schema import a_tile_item


def test_writing_an_item_creates_it(tmp_path):
    """One Item, one file, named by its id."""
    item = a_tile_item()
    path, written = stac_catalog.write_item(tmp_path, item)
    assert written
    assert path.name == f'{item["id"]}.json'
    assert json.loads(path.read_text())['id'] == item['id']


def test_an_unchanged_item_is_not_rewritten(tmp_path):
    """Design section 9: write only changed Items, so a replica syncs none."""
    item = a_tile_item()
    path, _ = stac_catalog.write_item(tmp_path, item)
    before = path.stat().st_mtime_ns
    path2, written = stac_catalog.write_item(tmp_path, item)
    assert path2 == path
    assert not written
    assert path.stat().st_mtime_ns == before


def test_a_changed_item_is_rewritten(tmp_path):
    """Content, never mtime: a changed fingerprint changes the file."""
    item = a_tile_item()
    stac_catalog.write_item(tmp_path, item)
    changed = a_tile_item(fingerprint_inputs={
        'source_ids': ['abc'], 'builder_version': 'v2'})
    _, written = stac_catalog.write_item(tmp_path, changed)
    assert written


def test_write_items_reports_only_what_changed(tmp_path):
    """The number a regenerate run reports is the number that moved."""
    items = [a_tile_item(), a_tile_item(row=4)]
    assert len(stac_catalog.write_items(tmp_path, items)) == 2
    assert stac_catalog.write_items(tmp_path, items) == []


def test_read_items_skips_the_collection(tmp_path):
    """``collection.json`` is not an Item."""
    stac_catalog.write_items(tmp_path, [a_tile_item()])
    stac_catalog.regenerate_collection(
        tmp_path, quantity=Quantity.DEPTHS, state=State.REVIEWED,
        origin=Origin.SURVEYED, description='test')
    assert len(stac_catalog.read_items(tmp_path)) == 1


def test_an_unreadable_item_is_loud(tmp_path):
    """A product the catalog silently stopped enumerating is worse."""
    (tmp_path / 'broken.json').write_text('{not json')
    with pytest.raises(stac_catalog.CatalogError):
        stac_catalog.read_items(tmp_path)


def test_regenerating_an_unchanged_collection_writes_nothing(tmp_path):
    """The whole point of the fingerprint-driven regenerate."""
    stac_catalog.write_items(tmp_path, [a_tile_item()])
    _, written, _ = stac_catalog.regenerate_collection(
        tmp_path, quantity=Quantity.DEPTHS, state=State.REVIEWED,
        origin=Origin.SURVEYED, description='test')
    assert written
    _, written, items = stac_catalog.regenerate_collection(
        tmp_path, quantity=Quantity.DEPTHS, state=State.REVIEWED,
        origin=Origin.SURVEYED, description='test')
    assert not written
    assert len(items) == 1


def test_the_collection_lands_where_the_layout_says(tmp_path):
    """One place decides where ``collection.json`` goes."""
    stac_catalog.write_items(tmp_path, [a_tile_item()])
    path, _, _ = stac_catalog.regenerate_collection(
        tmp_path, quantity=Quantity.DEPTHS, state=State.REVIEWED,
        origin=Origin.SURVEYED, description='test')
    assert path == layout.collection_path(tmp_path)


def test_an_item_with_no_id_is_refused(tmp_path):
    """It would overwrite whatever ``.json`` it landed on."""
    item = a_tile_item()
    del item['id']
    with pytest.raises(stac_catalog.CatalogError):
        stac_catalog.write_item(tmp_path, item, validate=False)


def test_a_malformed_item_is_refused(tmp_path):
    """Not a STAC object at all -- that is invalidity, not an unchecked box."""
    with pytest.raises(stac_catalog.CatalogError):
        stac_catalog.validate_item({'id': 'x', 'type': 'Feature'})


def test_an_undated_item_never_reaches_the_store(tmp_path):
    """
    The rule named at the point of writing, not as a schema error.

    An Item with a null ``datetime`` and no range is not a STAC Item, and
    pystac rejects it -- but as a message about a document the store should
    never have built. This refusal names what is actually wrong.
    """
    item = a_tile_item()
    item['properties'].pop('start_datetime')
    item['properties'].pop('end_datetime')
    with pytest.raises(stac_catalog.CatalogError) as caught:
        stac_catalog.write_item(tmp_path, item, validate=False)
    assert 'observation interval' in str(caught.value)
    assert not list(tmp_path.glob('*.json'))


def test_an_item_with_a_single_datetime_is_written(tmp_path):
    """STAC's other legal shape: an instant rather than a range."""
    item = a_tile_item()
    item['properties'].pop('start_datetime')
    item['properties'].pop('end_datetime')
    item['properties']['datetime'] = '2026-06-22T13:22:29Z'
    path, written = stac_catalog.write_item(tmp_path, item)
    assert written and path.is_file()
