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
Reading marine_tiled_raster_store's coverage manifest from Python.

The fixtures here are written in the exact shape ``saveCoverageManifest``
emits (``coverage-manifest/1``, run-encoded columns per row), so this is a
test of the shared convention rather than of a private schema.
"""

import json

from marine_world_store import coverage
import pytest


def write_manifest(directory, levels, kind='derived'):
    """Write a coverage-manifest/1 document as the C++ writer does."""
    path = directory / 'coverage.json'
    path.write_text(json.dumps(
        {'schema': 'coverage-manifest/1', 'kind': kind, 'levels': levels},
        indent=2))
    return path


def test_reads_runs_into_per_tile_errors(tmp_path):
    """A column run expands to one entry per tile, each with the run's error."""
    write_manifest(tmp_path, [
        {'level': 12, 'runs': [
            {'row': 4, 'col_min': 7, 'col_max': 9, 'geometric_error_m': 2.5}]},
    ])
    manifest = coverage.load_layer_coverage(tmp_path)
    assert len(manifest) == 3
    assert manifest.geometric_error((12, 4, 8)) == 2.5
    assert manifest.contains((12, 4, 7))
    assert not manifest.contains((12, 4, 10))


def test_an_unrecorded_error_is_none_not_zero(tmp_path):
    """Zero would read as a perfect tile; absent means "fall back"."""
    write_manifest(tmp_path, [
        {'level': 12, 'runs': [{'row': 1, 'col_min': 1, 'col_max': 1}]}])
    manifest = coverage.load_layer_coverage(tmp_path)
    assert manifest.geometric_error((12, 1, 1)) is None


def test_levels_and_tiles_are_ordered(tmp_path):
    """Coarsest level first; tiles in GGGS order."""
    write_manifest(tmp_path, [
        {'level': 13, 'runs': [{'row': 2, 'col_min': 2, 'col_max': 2}]},
        {'level': 11, 'runs': [{'row': 9, 'col_min': 0, 'col_max': 1}]},
    ])
    manifest = coverage.load_layer_coverage(tmp_path)
    assert manifest.levels() == [11, 13]
    assert manifest.tiles_at(11) == [(11, 9, 0), (11, 9, 1)]


def test_a_missing_manifest_reads_as_none(tmp_path):
    """Absent is normal: the manifest is derived and advisory (D8)."""
    assert coverage.load_layer_coverage(tmp_path) is None


def test_malformed_documents_read_as_none(tmp_path):
    """Tolerant in the same way the C++ reader is."""
    path = tmp_path / 'coverage.json'
    for text in ('not json', '[]', '{"schema": "other/1"}',
                 '{"schema": "coverage-manifest/1"}',
                 '{"schema": "coverage-manifest/1", "levels": [3]}'):
        path.write_text(text)
        assert coverage.load_layer_coverage(tmp_path) is None


def test_a_backwards_run_is_refused(tmp_path):
    """col_max < col_min is a corrupt document, not an empty run."""
    write_manifest(tmp_path, [
        {'level': 12, 'runs': [{'row': 1, 'col_min': 5, 'col_max': 2}]}])
    assert coverage.load_layer_coverage(tmp_path) is None


@pytest.mark.parametrize('error', [
    float('nan'), float('inf'), -1.0,
])
def test_an_error_that_is_not_a_length_refuses_the_document(tmp_path, error):
    """
    Refuse NaN, inf or a negative error, as the C++ reader does.

    Any of them would read as infinitely precise to an LOD consumer.
    Regression: they were accepted and handed to the Item as the tile's error.
    """
    write_manifest(tmp_path, [{'level': 12, 'runs': [
        {'row': 1, 'col_min': 2, 'col_max': 2, 'geometric_error_m': error}]}])
    assert coverage.load_layer_coverage(tmp_path) is None


def test_a_non_numeric_error_never_raises(tmp_path):
    """
    A string where a number belongs reads as unrecorded, like the C++ side.

    Regression: float() sat outside the try, so this raised despite the
    never-raises contract.
    """
    write_manifest(tmp_path, [{'level': 12, 'runs': [
        {'row': 1, 'col_min': 2, 'col_max': 2,
         'geometric_error_m': 'about two metres'}]}])
    manifest = coverage.load_layer_coverage(tmp_path)
    assert manifest.contains((12, 1, 2))
    assert manifest.geometric_error((12, 1, 2)) is None


@pytest.mark.parametrize('run', [
    {'row': 1.5, 'col_min': 2, 'col_max': 2},      # not an integer
    {'row': True, 'col_min': 2, 'col_max': 2},     # a bool is not an index
    {'row': -1, 'col_min': 2, 'col_max': 2},
    {'row': 1, 'col_min': 2, 'col_max': 2**32},    # past uint32
    {'row': 1, 'col_min': 0, 'col_max': 10_000_000},   # expansion cap
])
def test_an_index_the_cxx_reader_refuses_is_refused(tmp_path, run):
    """The two readers must agree on which documents are manifests."""
    write_manifest(tmp_path, [{'level': 12, 'runs': [run]}])
    assert coverage.load_layer_coverage(tmp_path) is None


def test_a_level_outside_gggs_is_refused(tmp_path):
    """GGGS levels are 0..20."""
    write_manifest(tmp_path, [{'level': 21, 'runs': [
        {'row': 0, 'col_min': 0, 'col_max': 0}]}])
    assert coverage.load_layer_coverage(tmp_path) is None


def test_scan_recovers_the_tile_set_with_no_errors(tmp_path):
    """A layer with no manifest still reads; a scan sees filenames only."""
    for name in ('12_1_1.tif', '12_1_2.tif', 'coverage.json'):
        (tmp_path / name).write_text('')
    manifest = coverage.scan_coverage(tmp_path)
    assert len(manifest) == 2
    assert manifest.geometric_error((12, 1, 1)) is None


def test_coverage_for_layer_prefers_the_manifest(tmp_path):
    """The manifest carries errors; the scan is only the fallback."""
    (tmp_path / '12_1_1.tif').write_text('')
    assert coverage.coverage_for_layer(tmp_path).kind == 'scanned'
    write_manifest(tmp_path, [
        {'level': 12, 'runs': [
            {'row': 1, 'col_min': 1, 'col_max': 1, 'geometric_error_m': 4.0}]},
    ], kind='derived')
    manifest = coverage.coverage_for_layer(tmp_path)
    assert manifest.kind == 'derived'
    assert manifest.geometric_error((12, 1, 1)) == 4.0
