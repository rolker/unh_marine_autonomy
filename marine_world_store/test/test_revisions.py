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

"""``revisions/`` records: append-only, content-identified, reviewable."""

import json

from marine_world_store import layout, revisions
from marine_world_store.revisions import RevisionError, RevisionKind
import pytest


def a_datum_record(**overrides):
    """Build the season's NAD83(2011)-labelled-WGS84 correction record."""
    kwargs = {
        'kind': RevisionKind.DATUM,
        'applies_to': {'source_id': 'abc123'},
        'parameters': {
            'height_offset_m': 1.19, 'from_frame': 'NAD83(2011)'},
        'evidence': 'PROJ transformation measured 2026-09-16',
        'reviewer': 'Roland Arsenault',
        'valid_from': '2026-06-22T00:00:00Z'}
    kwargs.update(overrides)
    return revisions.build_revision(**kwargs)


def test_id_is_the_content_hash():
    """Two records with the same content are the same record."""
    assert a_datum_record()['id'] == a_datum_record()['id']
    assert a_datum_record()['id'] != \
        a_datum_record(reviewer='Someone Else')['id']


def test_geometry_revision_carries_its_validity_interval():
    """A dated platform-geometry record (design section 6)."""
    record = revisions.build_revision(
        kind=RevisionKind.GEOMETRY,
        applies_to={'platform': 'bizzyboat'},
        parameters={'xyzrph': {'sonar_z': 0.135}},
        evidence='measured on the trailer 2026-08-21',
        reviewer='Roland Arsenault',
        valid_from='2026-08-21T00:00:00Z',
        valid_to='2026-09-01T00:00:00Z')
    properties = record['properties']
    assert properties['start_datetime'] == '2026-08-21T00:00:00Z'
    assert properties['end_datetime'] == '2026-09-01T00:00:00Z'


@pytest.mark.parametrize('field,value', [
    ('applies_to', {}),
    ('parameters', {}),
    ('evidence', '   '),
    ('reviewer', ''),
])
def test_a_record_nobody_could_re_examine_is_refused(field, value):
    """These are *reviewed* records; each field is what makes them one."""
    with pytest.raises(RevisionError):
        a_datum_record(**{field: value})


def test_an_unknown_kind_is_refused():
    """
    Refuse a kind this package does not write.

    Cleaning marks and decode revisions are the same shape, but only the two
    kinds the subset exercises are written here.
    """
    with pytest.raises(RevisionError):
        a_datum_record(kind='cleaning')


def test_write_is_idempotent(tmp_path):
    """Re-writing an identical record touches nothing."""
    record = a_datum_record()
    first = revisions.write_revision(tmp_path, record)
    before = first.read_bytes()
    second = revisions.write_revision(tmp_path, record)
    assert first == second
    assert second.read_bytes() == before


def test_records_land_under_revisions(tmp_path):
    """One record per file, named by its id."""
    record = a_datum_record()
    path = revisions.write_revision(tmp_path, record)
    assert path.parent == layout.revisions_dir(tmp_path)
    assert path.name == f'{record["id"]}.json'


def test_a_different_body_under_the_same_id_is_refused(tmp_path):
    """Append-only: a record is never edited in place."""
    record = a_datum_record()
    path = revisions.write_revision(tmp_path, record)
    tampered = json.loads(path.read_text())
    tampered['properties']['mws:revision']['reviewer'] = 'Someone Else'
    path.write_text(json.dumps(tampered))
    with pytest.raises(RevisionError):
        revisions.write_revision(tmp_path, record)


def test_reading_verifies_the_id_against_the_content(tmp_path):
    """An edited record is detected on read, not trusted."""
    path = revisions.write_revision(tmp_path, a_datum_record())
    assert revisions.read_revision(path)['id'] == path.stem
    body = json.loads(path.read_text())
    body['properties']['mws:revision']['parameters']['height_offset_m'] = 0.0
    path.write_text(json.dumps(body))
    with pytest.raises(RevisionError):
        revisions.read_revision(path)


def test_a_hand_assigned_id_is_refused(tmp_path):
    """The id is the content hash and is never chosen."""
    record = a_datum_record()
    record['id'] = 'something-i-made-up'
    with pytest.raises(RevisionError):
        revisions.write_revision(tmp_path, record)


def test_an_unquoted_yaml_time_in_applies_to_is_spelled_canonically(
        tmp_path):
    """
    A zoned datetime nested in applies_to is written as RFC 3339.

    Regression: YAML loads an unquoted timestamp as a datetime, and the id's
    canonical-JSON hash died on it with a raw TypeError traceback.
    """
    yaml = pytest.importorskip('yaml')
    applies_to = yaml.safe_load(
        'platform: bizzyboat\nfrom: 2026-06-01T00:00:00Z\n'
        'to: 2026-06-30T00:00:00Z\n')
    record = a_datum_record(applies_to=applies_to)
    body = record['properties']['mws:revision']['applies_to']
    assert body == {'platform': 'bizzyboat', 'from': '2026-06-01T00:00:00Z',
                    'to': '2026-06-30T00:00:00Z'}
    assert record['id'] == a_datum_record(applies_to={
        'platform': 'bizzyboat', 'from': '2026-06-01T00:00:00Z',
        'to': '2026-06-30T00:00:00Z'})['id']
    revisions.write_revision(tmp_path, record)


@pytest.mark.parametrize('text', [
    'from: 2026-06-01\n',                 # a date names a day, not an instant
    'from: 2026-06-01 12:00:00\n',        # no zone
])
def test_a_time_with_no_instant_in_applies_to_is_refused(text):
    """Refused with the remedy, never a traceback, never assumed UTC."""
    yaml = pytest.importorskip('yaml')
    with pytest.raises(RevisionError, match='applies_to.from'):
        a_datum_record(applies_to=yaml.safe_load(text))


def test_a_value_json_cannot_spell_is_refused_by_name():
    """A set (or any object) is named with where it sits."""
    with pytest.raises(RevisionError, match=r'parameters\.bad'):
        a_datum_record(parameters={'bad': {1, 2}})


def test_a_written_revision_is_a_valid_stac_item(tmp_path):
    """
    A revision names no Collection it cannot link, and validates as STAC.

    Regression: it carried ``collection: revisions`` with no links -- STAC 1.0
    requires a ``rel: collection`` link when ``collection`` is set, and no
    revisions Collection exists.
    """
    path = revisions.write_revision(tmp_path, a_datum_record())
    written = json.loads(path.read_text())
    assert 'collection' not in written
    assert not any(link.get('rel') == 'collection'
                   for link in written['links'])
    stac_catalog = pytest.importorskip(
        'marine_world_store.stac_catalog',
        reason='pystac is a declared dependency; rosdep')
    stac_catalog.validate_item(written)
