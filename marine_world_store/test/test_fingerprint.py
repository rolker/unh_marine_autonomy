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

"""Fingerprints: which inputs count, and how they are normalized."""

from marine_world_store.fingerprint import (
    canonical_json, content_hash, fingerprint, fingerprint_document,
    FingerprintError,
)
import pytest


def test_same_inputs_same_fingerprint():
    """The property everything else rests on."""
    assert fingerprint(source_ids=['a'], builder_version='1') == \
        fingerprint(source_ids=['a'], builder_version='1')


def test_changed_input_changes_the_fingerprint():
    """A changed process is a new fingerprint, never a migration."""
    assert fingerprint(source_ids=['a'], builder_version='1') != \
        fingerprint(source_ids=['a'], builder_version='2')


def test_source_order_does_not_matter():
    """Two builds over the same sources are the same build."""
    assert fingerprint(source_ids=['a', 'b'], builder_version='1') == \
        fingerprint(source_ids=['b', 'a'], builder_version='1')


def test_a_repeated_source_is_one_input():
    """
    Source and revision ids are sets: naming one twice is still one input.

    Regression: they were sorted but not de-duplicated, so a source listed
    twice gave the same build a different fingerprint.
    """
    assert fingerprint(source_ids=['a', 'b', 'a'], builder_version='1') == \
        fingerprint(source_ids=['a', 'b'], builder_version='1')
    assert fingerprint_document(
        source_ids=['a', 'a'], revision_ids=['r', 'r'],
        builder_version='1') == {
            'source_ids': ['a'], 'revision_ids': ['r'],
            'builder_version': '1'}


def test_consumer_ordering_order_does_matter():
    """Design section 5: the order a consumer used is an input."""
    assert fingerprint(consumer_ordering=['a', 'b'], builder_version='1') != \
        fingerprint(consumer_ordering=['b', 'a'], builder_version='1')


def test_the_schema_is_additive():
    """An input a stage does not have is omitted, not nulled."""
    document = fingerprint_document(source_ids=['a'], builder_version='1')
    assert set(document) == {'source_ids', 'builder_version'}


def test_omitting_and_nulling_are_not_the_same():
    """Passing None is a caller mistake, because it means neither."""
    with pytest.raises(FingerprintError):
        fingerprint(source_ids=['a'], trajectory_id=None)


def test_unknown_inputs_are_refused():
    """A typo'd input would make a fingerprint nothing else reproduces."""
    with pytest.raises(FingerprintError):
        fingerprint(source_id='a')


def test_a_fingerprint_over_nothing_is_refused():
    """It would identify every product equally."""
    with pytest.raises(FingerprintError):
        fingerprint()


def test_a_bare_string_where_a_list_belongs_is_refused():
    """``source_ids='abc'`` would hash per character and look fine."""
    with pytest.raises(FingerprintError):
        fingerprint(source_ids='abc')


def test_empty_collections_are_refused():
    """An empty list is not the same statement as an omitted input."""
    with pytest.raises(FingerprintError):
        fingerprint(source_ids=[], builder_version='1')


def test_blank_scalars_are_refused():
    """An empty builder version records nothing while looking recorded."""
    with pytest.raises(FingerprintError):
        fingerprint(source_ids=['a'], builder_version='  ')


def test_canonical_json_is_key_order_independent():
    """Two tools that agree on the inputs agree on the hash."""
    assert canonical_json({'b': 1, 'a': 2}) == canonical_json({'a': 2, 'b': 1})
    assert content_hash({'b': 1, 'a': 2}) == content_hash({'a': 2, 'b': 1})


def test_document_is_returned_as_well_as_hashed():
    """A consumer that disagrees can see which input it disagrees about."""
    document = fingerprint_document(
        source_ids=['b', 'a'], consumer_ordering=['x', 'y'],
        builder_version='1')
    assert document['source_ids'] == ['a', 'b']
    assert document['consumer_ordering'] == ['x', 'y']
