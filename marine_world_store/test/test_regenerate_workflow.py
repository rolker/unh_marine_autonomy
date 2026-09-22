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
The regenerate workflow's Python half: fingerprints, records, and the rules.

What these protect is the claim design section 9 makes -- that a regenerate is
driven by FINGERPRINTS and rebuilds only what changed -- on top of a DAG engine
that decides from mtimes. If the pre-step does not reconcile the two, every
rule above a rewritten-but-identical tile re-runs, which is the behaviour the
prototype found and this workflow exists to fix.

The Snakemake rules themselves are checked statically here and, when snakemake
is importable, with a dry run. It is not installed on the development host
(``snakemake`` resolves through the repo-root ``rosdep.yaml`` local key), so the
dry run skips with that reason rather than being deleted -- a test that cannot
run today still has to exist for the day the dependency lands.
"""

import json
import os
from pathlib import Path

from marine_world_store import fingerprint_sidecar, overview_records

import pytest

SNAKEMAKE_DIR = Path(__file__).resolve().parents[1] / 'snakemake'


def _tile(directory: Path, name: str, content: bytes = b'tile') -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_bytes(content)
    return path


# --- the fingerprint pre-step -----------------------------------------------


def test_first_pass_records_without_claiming_the_tile_is_older(tmp_path):
    """A first run has nothing to compare against, so it moves no mtime."""
    tile = _tile(tmp_path, '13_1_1.tif')
    before = tile.stat().st_mtime
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.created == 1 and report.unchanged == 0
    assert tile.stat().st_mtime == before
    document = json.loads(fingerprint_sidecar.sidecar_path(tile).read_text())
    assert document['schema'] == fingerprint_sidecar.SCHEMA
    assert document['fingerprint'] == fingerprint_sidecar.content_fingerprint(
        tile)


def test_a_byte_identical_rewrite_stops_looking_like_a_change(tmp_path):
    """
    Reset an unchanged tile's mtime to its sidecar's.

    This is the whole point of the pre-step: a rebuild that produced the same
    bytes must not make every rule above the tile re-run.
    """
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.refresh_directory(tmp_path)
    sidecar_mtime = fingerprint_sidecar.sidecar_path(tile).stat().st_mtime
    # A rebuild: same bytes, newer mtime.
    tile.write_bytes(b'tile')
    os.utime(tile, (sidecar_mtime + 1000, sidecar_mtime + 1000))
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.unchanged == 1 and report.changed == 0
    assert tile.stat().st_mtime == pytest.approx(sidecar_mtime)


def test_a_real_change_keeps_its_mtime_and_rewrites_the_record(tmp_path):
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.refresh_directory(tmp_path)
    tile.write_bytes(b'different')
    changed_at = tile.stat().st_mtime
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.changed == 1 and report.unchanged == 0
    assert tile.stat().st_mtime == pytest.approx(changed_at)
    assert json.loads(
        fingerprint_sidecar.sidecar_path(tile).read_text())['fingerprint'] == \
        fingerprint_sidecar.content_fingerprint(tile)


def test_an_unreadable_sidecar_is_named_not_silently_replaced(tmp_path):
    """
    Treat an unreadable record as absent, and say which one it was.

    The tile will look changed to the DAG once, so the operator should be able
    to tell that from a genuine rebuild.
    """
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.sidecar_path(tile).write_text('{ not json')
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.created == 1
    assert report.unreadable == [str(fingerprint_sidecar.sidecar_path(tile))]


def test_a_sidecar_of_another_schema_is_not_interpreted(tmp_path):
    """A document this module did not write is not its to read fields out of."""
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.sidecar_path(tile).write_text(
        json.dumps({'schema': 'something-else/1', 'fingerprint': 'deadbeef'}))
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.created == 1
    assert json.loads(
        fingerprint_sidecar.sidecar_path(tile).read_text())['schema'] == \
        fingerprint_sidecar.SCHEMA


def test_an_orphan_sidecar_is_removed(tmp_path):
    """A .fp whose tile is gone asserts a fingerprint nothing can check."""
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.refresh_directory(tmp_path)
    tile.unlink()
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.orphans_removed == 1
    assert not fingerprint_sidecar.sidecar_path(tile).exists()


def test_a_layer_reports_its_native_tiles_and_overviews_apart(tmp_path):
    """
    Report the two scopes separately: they are different kinds of thing.

    The native tiles are a compile this store does not own; the overviews are
    the derived product the DAG rebuilds.
    """
    _tile(tmp_path, '13_1_1.tif')
    _tile(tmp_path / 'overviews', '12_0_0.tif')
    reports = fingerprint_sidecar.refresh_layer(tmp_path)
    assert set(reports) == {'native', 'overviews'}
    assert reports['native'].created == 1
    assert reports['overviews'].created == 1


# --- the per-tile records and the manifest they assemble into ---------------


def _record(directory: Path, name: str, error, sigma_fold='undecided'):
    _tile(directory, name + '.tif')
    (directory / (name + '.json')).write_text(json.dumps({
        'schema': overview_records.TILE_RECORD_SCHEMA,
        'geometric_error_m': error,
        'bands': ['min', 'mean', 'count', 'sigma'],
        'sigma_fold': sigma_fold,
        'sigma_band_written': False,
    }))


def test_records_assemble_into_the_manifest_the_cxx_reader_expects(tmp_path):
    _record(tmp_path, '12_5_7', 4.0)
    _record(tmp_path, '12_5_8', 4.0)
    _record(tmp_path, '12_5_9', 9.0)
    _record(tmp_path, '11_2_3', 9.0)
    path = overview_records.assemble(tmp_path)
    document = json.loads(path.read_text())
    assert document['schema'] == overview_records.MANIFEST_SCHEMA
    assert document['kind'] == 'derived'
    assert [level['level'] for level in document['levels']] == [11, 12]
    runs = document['levels'][1]['runs']
    # Adjacent columns merge ONLY where the error agrees, so the error stays
    # per tile rather than widening to a per-row maximum.
    assert runs[0] == {'row': 5, 'col_min': 7, 'col_max': 8,
                       'geometric_error_m': 4.0}
    assert runs[1] == {'row': 5, 'col_min': 9, 'col_max': 9,
                       'geometric_error_m': 9.0}


def test_an_unrecorded_error_is_none_never_zero(tmp_path):
    """uma-ADR-0013 D1: absence means 'fall back', not 'no error'."""
    _record(tmp_path, '12_5_7', None)
    document = json.loads(overview_records.assemble(tmp_path).read_text())
    assert document['levels'][0]['runs'][0]['geometric_error_m'] is None


def test_a_record_whose_tile_is_gone_advertises_no_coverage(tmp_path):
    _record(tmp_path, '12_5_7', 4.0)
    (tmp_path / '12_5_7.tif').unlink()
    document = json.loads(overview_records.assemble(tmp_path).read_text())
    assert document['levels'] == []


def test_a_foreign_or_malformed_record_is_skipped(tmp_path):
    _tile(tmp_path, '12_5_7.tif')
    (tmp_path / '12_5_7.json').write_text(
        json.dumps({'schema': 'other/1', 'geometric_error_m': 1.0}))
    _tile(tmp_path, '12_5_8.tif')
    (tmp_path / '12_5_8.json').write_text('{ not json')
    assert overview_records.read_tile_records(tmp_path) == {}


def test_the_sigma_rules_in_use_are_counted_not_summarised(tmp_path):
    """
    Count the rules, so a layer built across a rule change is visible.

    That is a new fingerprint and not a migration; one number would read as the
    whole layer's.
    """
    _record(tmp_path, '12_5_7', 4.0, sigma_fold='undecided')
    _record(tmp_path, '12_5_8', 4.0, sigma_fold='pooled')
    assert overview_records.sigma_folds(tmp_path) == {
        'undecided': 1, 'pooled': 1}


def test_assemble_refuses_a_directory_that_is_not_there(tmp_path):
    with pytest.raises(OSError):
        overview_records.assemble(tmp_path / 'absent')


# --- the rules --------------------------------------------------------------


def test_the_workflow_files_are_all_present():
    """A rule file lost to a rename is a workflow that silently does less."""
    assert (SNAKEMAKE_DIR / 'Snakefile').is_file()
    for name in ('fingerprints', 'overviews', 'catalog', 'gti'):
        assert (SNAKEMAKE_DIR / 'rules' / f'{name}.smk').is_file()


def test_the_rules_invoke_the_tools_by_their_entry_point_names():
    """
    Call the installed console scripts, not module paths.

    The CLIs are `console_scripts`, so they work with or without `ros2 run`;
    a rule that invoked `python3 -m ...` would work only where this checkout is
    on the path.
    """
    text = '\n'.join(
        path.read_text()
        for path in sorted((SNAKEMAKE_DIR / 'rules').glob('*.smk')))
    for tool in ('mws_refresh_fingerprints', 'mws_assemble_coverage',
                 'mws_regenerate_catalog', 'build_depth_overview_parent'):
        assert tool in text, tool
    assert 'python3 -m' not in text


def test_the_parent_enumeration_is_not_reimplemented_in_the_rules():
    """
    Keep the GGGS parent/child mapping on the C++ side.

    Its column counts vary by latitude band; a second implementation in a
    workflow file would be a second thing to get wrong, with no tests on it.
    """
    text = (SNAKEMAKE_DIR / 'rules' / 'overviews.smk').read_text()
    assert '--list-parents' in text
    for reimplementation in ('row // 2', 'col // 2', 'row//2', 'col//2'):
        assert reimplementation not in text


def test_the_layer_directory_has_no_default():
    """
    Require `layer_dir`, rather than defaulting it.

    The store root is resolved by `marine_world_store.store_root`; a path
    written into the workflow would be the hard-coded path the guard test
    forbids.
    """
    text = (SNAKEMAKE_DIR / 'Snakefile').read_text()
    assert 'layer_dir is required' in text
    assert 'data/world' not in text


def test_snakemake_accepts_the_workflow(tmp_path):
    """
    Dry-run the workflow, when snakemake is installed.

    Skipped on a host that has not had `rosdep install` run for this repo --
    `snakemake` resolves through the repo-root rosdep.yaml local key. The test
    exists for the day the dependency lands; deleting it would mean the rules
    were never machine-checked at all.
    """
    pytest.importorskip(
        'snakemake',
        reason='snakemake is a declared dependency; run rosdep install.')
    import subprocess
    layer = tmp_path / 'depths' / 'reviewed' / 'surveyed'
    _tile(layer, '13_1_1.tif')
    result = subprocess.run(
        ['snakemake', '-s', str(SNAKEMAKE_DIR / 'Snakefile'), '--dry-run',
         '--config', f'layer_dir={layer}', 'fine_level=13', 'min_level=11'],
        capture_output=True, text=True, cwd=str(tmp_path))
    assert result.returncode == 0, result.stderr
