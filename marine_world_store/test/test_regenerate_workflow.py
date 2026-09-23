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

The Snakemake rules themselves are checked statically here, and end to end by
running snakemake for real over a toy layer (``snakemake`` is a declared
dependency, resolved through the repo-root ``rosdep.yaml`` local key; the e2e
tests skip only where it is not installed).
"""

import json
import os
from pathlib import Path
import re
import shutil
import time

from marine_world_store import fingerprint_sidecar, overview_records

import pytest

SNAKEMAKE_DIR = Path(__file__).resolve().parents[1] / 'snakemake'


def _tile(directory: Path, name: str, content: bytes = b'tile') -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_bytes(content)
    return path


# --- the fingerprint pre-step -----------------------------------------------


def test_a_first_pass_counts_every_tile_as_a_change(tmp_path):
    """
    With nothing recorded, nothing proves a product was built from this tile.

    So the tile reads as changed: its mtime is advanced to now, and recorded.
    """
    tile = _tile(tmp_path, '13_1_1.tif')
    os.utime(tile, ns=(10**18, 10**18))       # an old compile, copied in
    before = time.time_ns()
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.created == 1 and report.unchanged == 0
    assert tile.stat().st_mtime_ns >= before
    document = json.loads(fingerprint_sidecar.sidecar_path(tile).read_text())
    assert document['schema'] == fingerprint_sidecar.SCHEMA
    assert document['fingerprint'] == fingerprint_sidecar.content_fingerprint(
        tile)
    assert document['mtime_ns'] == tile.stat().st_mtime_ns


def test_a_byte_identical_rewrite_stops_looking_like_a_change(tmp_path):
    """
    Reset an unchanged tile's mtime to the one recorded with its content.

    This is the whole point of the pre-step: a rebuild that produced the same
    bytes must not make every rule above the tile re-run.
    """
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.refresh_directory(tmp_path)
    recorded = tile.stat().st_mtime_ns
    # A rebuild: same bytes, newer mtime.
    tile.write_bytes(b'tile')
    os.utime(tile, ns=(recorded + 10**12, recorded + 10**12))
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.unchanged == 1 and report.changed == 0
    assert tile.stat().st_mtime_ns == recorded


def test_unchanged_tiles_keep_the_order_they_were_built_in(tmp_path):
    """
    A parent built after its child stays newer than it, run after run.

    Regression: unchanged tiles were reset to their SIDECAR's mtime, and the
    refresh writes coarser tiles' sidecars first -- so every unchanged parent
    came out older than its unchanged child, and the DAG rebuilt it byte for
    byte on every run, forever.
    """
    overviews = tmp_path / 'overviews'
    child = _tile(overviews, '13_1_1.tif', b'child')
    parent = _tile(overviews, '12_0_0.tif', b'parent')
    os.utime(child, ns=(10**18, 10**18))
    os.utime(parent, ns=(10**18 + 5, 10**18 + 5))   # built after its child
    fingerprint_sidecar.record_tile(child)
    fingerprint_sidecar.record_tile(parent)
    for _ in range(3):
        fingerprint_sidecar.refresh_directory(overviews)
        assert parent.stat().st_mtime_ns > child.stat().st_mtime_ns


def test_a_real_change_is_newer_than_anything_built_before_it(tmp_path):
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.refresh_directory(tmp_path)
    tile.write_bytes(b'different')
    before = time.time_ns()
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.changed == 1 and report.unchanged == 0
    assert tile.stat().st_mtime_ns >= before
    document = json.loads(fingerprint_sidecar.sidecar_path(tile).read_text())
    assert document['fingerprint'] == \
        fingerprint_sidecar.content_fingerprint(tile)
    assert document['mtime_ns'] == tile.stat().st_mtime_ns


def test_a_change_copied_in_with_an_old_mtime_is_still_a_change(tmp_path):
    """
    Decide from the content; the mtime a file arrives with proves nothing.

    Regression: a changed tile kept the mtime it arrived with. ``copy2`` (the
    adapter's copy), ``rsync -t`` and a restore all preserve an OLD mtime, so a
    re-linked older compile looked older than the overviews built from the
    content it replaced, and the DAG said "Nothing to be done".
    """
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.refresh_directory(tmp_path)
    recorded = tile.stat().st_mtime_ns
    older = tmp_path / 'older.tif'
    older.write_bytes(b'an older compile')
    os.utime(older, ns=(10**18, 10**18))
    shutil.copy2(older, tile)
    assert tile.stat().st_mtime_ns < recorded
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.changed == 1
    assert tile.stat().st_mtime_ns > recorded


def test_a_built_tile_is_recorded_with_the_mtime_its_build_gave_it(tmp_path):
    """
    ``record_tile`` records; it does not compare, and it moves no mtime.

    Regression: a parent rebuilt byte for byte (its child changed only where
    the fold does not look) was reset to its FIRST-seen mtime -- older than the
    child whose change it had absorbed -- so the DAG rebuilt it, and every
    ancestor, on every later run.
    """
    parent = _tile(tmp_path, '12_0_0.tif', b'parent')
    os.utime(parent, ns=(10**18, 10**18))
    fingerprint_sidecar.record_tile(parent)
    # Rebuilt, byte for byte, after its child changed.
    parent.write_bytes(b'parent')
    built_at = parent.stat().st_mtime_ns
    fingerprint_sidecar.record_tile(parent)
    assert parent.stat().st_mtime_ns == built_at
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.unchanged == 1
    assert parent.stat().st_mtime_ns == built_at


def test_a_symbolic_link_is_not_recorded_as_built(tmp_path):
    real = _tile(tmp_path, 'real.tif')
    link = tmp_path / '12_0_0.tif'
    link.symlink_to(real)
    with pytest.raises(OSError, match='symbolic link'):
        fingerprint_sidecar.record_tile(link)


def test_a_superseded_sidecar_is_re_recorded_quietly(tmp_path):
    """A /1 sidecar has no recorded mtime; it is re-recorded, not 'unreadable'."""
    tile = _tile(tmp_path, '13_1_1.tif')
    fingerprint_sidecar.sidecar_path(tile).write_text(json.dumps({
        'schema': 'tile-content-fingerprint/1',
        'fingerprint': fingerprint_sidecar.content_fingerprint(tile)}))
    report = fingerprint_sidecar.refresh_directory(tmp_path)
    assert report.created == 1 and report.unreadable == []


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
    derived = _tile(tmp_path / 'overviews', '12_0_0.tif')
    fingerprint_sidecar.record_tile(derived)
    reports = fingerprint_sidecar.refresh_layer(tmp_path)
    assert set(reports) == {'native', 'overviews'}
    assert reports['native'].created == 1
    assert reports['overviews'].unchanged == 1


def test_a_first_refresh_removes_every_overview(tmp_path):
    """No sidecars prove any overview was built from these natives: redo all."""
    native = _tile(tmp_path, '13_1_1.tif', b'native')
    derived = _tile(tmp_path / 'overviews', '12_0_0.tif', b'derived')
    record = _tile(tmp_path / 'overviews', '12_0_0.json', b'{}')
    reports = fingerprint_sidecar.refresh_layer(tmp_path)
    assert reports['overviews'].removed == [str(derived)]
    assert not derived.exists() and not record.exists()
    assert native.exists()


@pytest.mark.parametrize('damage', ['changed', 'no-sidecar', 'unreadable'])
def test_a_derived_tile_not_built_from_what_is_there_is_removed(
        tmp_path, damage):
    """
    Remove a derived tile whose content is not what the DAG recorded.

    Regression: it was classed "changed" and advanced to now, like a native
    tile -- newer than its children, so it was never rebuilt, while its
    parents were rebuilt FROM its stale content (a restore from a backup, a
    partial copy, bit rot).
    """
    overviews = tmp_path / 'overviews'
    derived = _tile(overviews, '12_0_0.tif', b'built')
    record = _tile(overviews, '12_0_0.json', b'{}')
    fingerprint_sidecar.record_tile(derived)
    sidecar = fingerprint_sidecar.sidecar_path(derived)
    if damage == 'changed':
        derived.write_bytes(b'restored from an old backup')
    elif damage == 'no-sidecar':
        sidecar.unlink()
    else:
        sidecar.write_text('{ not json')
    report = fingerprint_sidecar.refresh_directory(overviews, derived=True)
    assert report.removed == [str(derived)]
    assert report.changed == 0 and report.created == 0
    assert not derived.exists()
    assert not record.exists()
    assert not sidecar.exists()


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


def test_an_unchanged_manifest_is_left_alone(tmp_path):
    """
    Reassembling the same records leaves coverage.json's bytes and mtime.

    The DAG reassembles on every run; a rewrite that changed nothing would
    still make a replica sync move the file (design section 9).
    """
    _record(tmp_path, '12_5_7', 4.0)
    path = overview_records.assemble(tmp_path)
    os.utime(path, ns=(10**18, 10**18))
    overview_records.assemble(tmp_path)
    assert path.stat().st_mtime_ns == 10**18
    _record(tmp_path, '12_5_8', 4.0)
    overview_records.assemble(tmp_path)
    assert path.stat().st_mtime_ns != 10**18


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


def test_a_tile_with_no_record_is_still_coverage(tmp_path):
    """
    Keep an unrecorded tile in the manifest, with the error it already had.

    Regression: assembly dropped every tile with no per-tile record (a
    batch-built pyramid records errors only in coverage.json), rewriting the
    manifest with fewer tiles than the directory holds.
    """
    _record(tmp_path, '12_5_7', 4.0)
    _tile(tmp_path, '12_5_8.tif')          # batch-built: no record
    _tile(tmp_path, '12_5_9.tif')          # no record and no prior entry
    (tmp_path / 'coverage.json').write_text(json.dumps({
        'schema': overview_records.MANIFEST_SCHEMA, 'kind': 'derived',
        'levels': [{'level': 12, 'runs': [
            {'row': 5, 'col_min': 8, 'col_max': 8,
             'geometric_error_m': 6.5}]}]}))
    records, unrecorded = overview_records.manifest_records(tmp_path)
    assert unrecorded == [(12, 5, 8), (12, 5, 9)]
    document = json.loads(overview_records.assemble(tmp_path).read_text())
    runs = document['levels'][0]['runs']
    covered = {(r['row'], c): r['geometric_error_m']
               for r in runs for c in range(r['col_min'], r['col_max'] + 1)}
    assert covered == {(5, 7): 4.0, (5, 8): 6.5, (5, 9): None}


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
    for name in ('overviews', 'catalog', 'gti'):
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
        for path in [SNAKEMAKE_DIR / 'Snakefile',
                     *sorted((SNAKEMAKE_DIR / 'rules').glob('*.smk'))])
    for tool in ('mws_refresh_fingerprints', 'mws_assemble_coverage',
                 'mws_regenerate_catalog', 'mws_list_tiles',
                 'build_depth_overview_parent'):
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


def test_every_shell_path_is_quoted():
    """A layer path with a space must not split into two arguments."""
    for path in sorted((SNAKEMAKE_DIR / 'rules').glob('*.smk')):
        for field in re.findall(r'\{(params\.[a-z_]+|output|input)(:q)?\}',
                                path.read_text()):
            name, quoted = field
            if name in ('params.remove_levels', 'params.kind'):
                continue   # integers and a fixed word, never a path
            assert quoted, f'{path.name}: {{{name}}} is not :q-quoted'


# --- the workflow, end to end ------------------------------------------------
#
# These run snakemake for real, over a toy layer, with a stand-in for the C++
# per-parent tool (fake_build_depth_overview_parent.py -- same command-line
# contract, toy quadtree). The mws_* tools are the real ones, reached through
# PATH wrappers so the test needs no install space. What is under test is the
# DAG: that a regenerate after the first rebuilds exactly what changed.

TEST_DIR = Path(__file__).resolve().parent
PACKAGE_DIR = TEST_DIR.parent
MWS_TOOLS = ('mws_refresh_fingerprints', 'mws_assemble_coverage',
             'mws_regenerate_catalog', 'mws_list_tiles')


def _executable(path: Path, text: str) -> Path:
    path.write_text(text)
    path.chmod(0o755)
    return path


@pytest.fixture
def workflow(tmp_path, monkeypatch):
    """Provide a layer, tool wrappers on PATH, and a snakemake runner."""
    pytest.importorskip('snakemake', reason='declared dependency; rosdep')
    pytest.importorskip('pystac', reason='declared dependency; rosdep')
    gdal = pytest.importorskip('osgeo.gdal', reason='declared dependency')
    gdal.UseExceptions()
    import shutil
    import subprocess
    import sys
    if shutil.which('gdaltindex') is None:
        pytest.skip('gdaltindex (GDAL) is a declared dependency')

    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    for tool in MWS_TOOLS:
        _executable(bin_dir / tool, (
            f'#!{sys.executable}\n'
            'import sys\n'
            f'sys.path.insert(0, {str(PACKAGE_DIR)!r})\n'
            f'from marine_world_store.cli.{tool} import console_main\n'
            'sys.exit(console_main())\n'))
    fake = _executable(
        bin_dir / 'fake_build_depth_overview_parent',
        f'#!{sys.executable}\n'
        'import runpy, sys\n'
        f'sys.argv[0] = {str(TEST_DIR / "fake_build_depth_overview_parent.py")!r}\n'
        'runpy.run_path(sys.argv[0], run_name="__main__")\n')
    log = tmp_path / 'tool.log'
    monkeypatch.setenv('PATH', f'{bin_dir}{os.pathsep}{os.environ["PATH"]}')
    monkeypatch.setenv('FAKE_TOOL_LOG', str(log))
    # The catalog must regenerate the layer the DAG built -- never the tree
    # the environment's store root names. Point that somewhere to watch.
    elsewhere = tmp_path / 'the-environments-store'
    monkeypatch.setenv('WORLD_STORE_ROOT', str(elsewhere))

    layer = tmp_path / 'store' / 'depths' / 'reviewed' / 'surveyed'
    layer.mkdir(parents=True)

    class Workflow:

        def __init__(self):
            self.layer = layer
            self.elsewhere = elsewhere
            self.log = log

        def native(self, name, value=1.0, sigma=0.5, path=None):
            """
            Write a native tile and its Item, as the link step would.

            128 x 128 x 2 float64 = 256 kB: over Snakemake's 100 kB checksum
            limit, as real tiles are. Below it Snakemake compares checksums
            itself, and a test of the fingerprint pre-step would pass with
            the pre-step switched off.
            """
            from marine_world_store import item_schema, stac_catalog
            path = path or layer / name
            dataset = gdal.GetDriverByName('GTiff').Create(
                str(path), 128, 128, 2, gdal.GDT_Float64)
            dataset.SetGeoTransform((-70.0, 0.001, 0.0, 42.1, 0.0, -0.001))
            dataset.GetRasterBand(1).Fill(value)
            dataset.GetRasterBand(2).Fill(sigma)
            dataset = None
            assert path.stat().st_size > 100_000
            if path.parent != layer:
                return path
            level, row, col = (int(p) for p in name[:-4].split('_'))
            stac_catalog.write_items(layer, [item_schema.build_tile_item(
                quantity='depths', state='reviewed', origin='surveyed',
                level=level, row=row, col=col, asset_href=f'./{name}',
                fingerprint_inputs={'source_ids': [f'bag-{name}'],
                                    'builder_version': 'test/1'},
                uncertainty_basis='test', resolution_m=1.0,
                start_datetime='2026-06-22T13:00:00Z',
                end_datetime='2026-06-22T14:00:00Z')])
            return path

        def invoke(self, *extra, **config):
            """Run snakemake over the layer; return the CompletedProcess."""
            self.log.write_text('')
            settings = {'layer_dir': layer, 'fine_level': 13, 'min_level': 11,
                        'build_depth_overview_parent_tool': fake, **config}
            return subprocess.run(
                ['snakemake', '-s', str(SNAKEMAKE_DIR / 'Snakefile'),
                 '--cores', '2', '--config',
                 *(f'{k}={v}' for k, v in settings.items()), *extra],
                capture_output=True, text=True, cwd=str(tmp_path))

        def refused(self, *extra, **config):
            """Run the workflow expecting a refusal; return its output."""
            result = self.invoke(*extra, **config)
            output = result.stdout + result.stderr
            assert result.returncode != 0, output
            return output

        def run(self, *extra, **config):
            """Run the workflow; return (stdout+stderr, builds, prunes)."""
            result = self.invoke(*extra, **config)
            output = result.stdout + result.stderr
            assert result.returncode == 0, output
            lines = self.log.read_text().split()
            builds = sorted(lines[i + 1] for i, w in enumerate(lines)
                            if w == 'build')
            prunes = sorted(lines[i + 1] for i, w in enumerate(lines)
                            if w in ('prune', 'remove'))
            return output, builds, prunes

        def published(self):
            """Every published product file: (path, bytes, mtime)."""
            found = []
            for path in sorted(layer.rglob('*.json')):
                if '.regenerate' not in path.parts:
                    found.append((path.relative_to(layer), path.read_bytes(),
                                  path.stat().st_mtime_ns))
            return found

    return Workflow()


def test_a_regenerate_rebuilds_exactly_what_changed(workflow):
    """
    Rebuild what changed, and nothing else -- run after run.

    Regression: every rule's inputs and outputs were .done stamps, so after
    the first run a rewritten tile produced "Nothing to be done", exit 0.
    """
    for name in ('13_0_0.tif', '13_0_1.tif', '13_1_0.tif', '13_1_1.tif',
                 '13_2_2.tif'):
        workflow.native(name)
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0', '12_1_1']

    # Nothing changed: nothing is rebuilt, and nothing published is touched
    # -- not a byte, not an mtime (design section 9's replica rule). The
    # listings, manifest assembly, catalog and indexes are re-derived every
    # run; they write only what changed.
    before = workflow.published()
    output, builds, _ = workflow.run()
    assert builds == [], output
    assert workflow.published() == before

    # A real change rebuilds its ancestors and ONLY them.
    workflow.native('13_2_2.tif', value=7.0)
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_1_1']
    _, builds, _ = workflow.run()
    assert builds == []


def _rewrite_identically(tile):
    """Rewrite ``tile`` byte for byte, with a newer mtime (a rebuild)."""
    tile.write_bytes(tile.read_bytes())
    stat = tile.stat()
    os.utime(tile, ns=(stat.st_atime_ns, stat.st_mtime_ns + 10**11))


def test_a_byte_identical_rewrite_is_not_a_change(workflow):
    """
    The fingerprint pre-step, not Snakemake, is what keeps this quiet.

    Pinned both ways, so this test can fail: with the pre-step switched off
    the same rewrite DOES rebuild (the tiles are over Snakemake's 100 kB
    checksum limit, so its own checksum shortcut cannot hide the difference).
    Regression: the first version of this test used 4 x 4 tiles and passed
    with the pre-step disabled.
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    tile = workflow.layer / '13_0_0.tif'

    _rewrite_identically(tile)
    _, builds, _ = workflow.run()
    assert builds == []

    _rewrite_identically(tile)
    _, builds, _ = workflow.run(refresh_fingerprints='false')
    assert builds == ['11_0_0', '12_0_0']


def test_a_change_copied_in_with_an_old_mtime_rebuilds(workflow):
    """
    Changed content is a change, whatever mtime the file arrived with.

    Regression: the pre-step saw the change and left the tile's mtime alone.
    The adapter copies with ``copy2``, which keeps the source's mtime, so a
    re-linked older compile (or an rsync -t, or a restore) was older than the
    overviews built from what it replaced: "Nothing to be done".
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    older = workflow.native('older.tif', value=9.0,
                            path=workflow.layer.parent / 'older.tif')
    os.utime(older, ns=(10**18, 10**18))
    workflow.run()
    shutil.copy2(older, workflow.layer / '13_0_0.tif')
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0']
    _, builds, _ = workflow.run()
    assert builds == []


def test_a_rebuilt_but_identical_parent_does_not_rebuild_forever(workflow):
    """
    A child that changes where the fold does not look rebuilds its parent once.

    Here the sigma band: the fold (like the real tool's undecided sigma rule)
    gives the parent the same bytes. Regression: that parent was reset to its
    first-seen mtime, older than the changed child, so it and every ancestor
    were rebuilt on every later run.
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    workflow.native('13_0_0.tif', sigma=0.9)
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0']
    for _ in range(2):
        _, builds, _ = workflow.run()
        assert builds == []


def _band1(path):
    from osgeo import gdal
    dataset = gdal.Open(str(path))
    return float(dataset.GetRasterBand(1).ReadAsArray(0, 0, 1, 1)[0][0])


def test_a_derived_tile_restored_from_a_backup_is_rebuilt(workflow, tmp_path):
    """
    A derived tile whose content is not the one built is rebuilt, not trusted.

    Regression: the pre-step advanced it to now like a changed native tile --
    newer than its children, so it was never rebuilt, while its parent was
    rebuilt from the stale content. Both reviewers' cases: a copy2-restored
    older tile, and a single flipped byte.
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    overviews = workflow.layer / 'overviews'
    derived = overviews / '12_0_0.tif'
    backup = tmp_path / 'backup.tif'
    shutil.copy2(derived, backup)
    workflow.native('13_0_0.tif', value=7.0)
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0']
    assert _band1(derived) == 7.0

    shutil.copy2(backup, derived)                 # an operator's restore
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0']
    assert _band1(derived) == 7.0
    assert _band1(overviews / '11_0_0.tif') == 7.0 + 1.0

    content = bytearray(derived.read_bytes())     # bit rot
    content[-1] ^= 0xff
    derived.write_bytes(bytes(content))
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0']
    _, builds, _ = workflow.run()
    assert builds == []


def test_a_deleted_product_is_rebuilt(workflow):
    """
    A derived tile or its record deleted since the last run is rebuilt.

    Regression: with every product downstream present, Snakemake asked for no
    listing, so it never saw that a scheduled parent's output was missing:
    "Nothing to be done", every run, while coverage.json and the overview
    Item kept advertising the tile.
    """
    for name in ('13_0_0.tif', '13_0_1.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    overviews = workflow.layer / 'overviews'

    # Rebuilt, and so is the parent that folds it (Snakemake cannot know in
    # the same run that the rebuild came out the same).
    (overviews / '12_0_0.tif').unlink()
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_0_0']
    assert (overviews / '12_0_0.tif').is_file()

    (overviews / '12_1_1.json').unlink()
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0', '12_1_1']
    assert (overviews / '12_1_1.json').is_file()

    # The coarsest level's, too: nothing above it asks for them but the
    # manifest.
    (overviews / '11_0_0.tif').unlink()
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0']
    (overviews / '11_0_0.json').unlink()
    _, builds, _ = workflow.run()
    assert builds == ['11_0_0']

    # A published product of the bookkeeping steps is re-derived too.
    item = workflow.layer / 'depths-reviewed-surveyed-11_0_0.json'
    item.unlink()
    (workflow.layer / 'index.gti.fgb').unlink()
    _, builds, _ = workflow.run()
    assert builds == []
    assert item.is_file()
    assert (workflow.layer / 'index.gti.fgb').is_file()


def test_levels_outside_the_configured_range_are_removed(workflow):
    """
    Derived levels this run does not build are removed, not left published.

    Regression: after min_level was raised, the old coarser levels were never
    pruned or rebuilt (they still have children), yet the manifest and the
    overview Items scan all of overviews/, so they stayed published, stale.
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    overviews = workflow.layer / 'overviews'
    assert (overviews / '11_0_0.tif').is_file()

    _, builds, removed = workflow.run(min_level=12)
    assert removed == ['11_0_0']
    assert builds == []
    assert not (overviews / '11_0_0.tif').exists()
    assert not (overviews / '11_0_0.json').exists()
    coverage = json.loads((overviews / 'coverage.json').read_text())
    assert [lvl['level'] for lvl in coverage['levels']] == [12]
    assert not (workflow.layer / 'depths-reviewed-surveyed-11_0_0.json').exists()


def test_a_fine_level_the_natives_contradict_is_refused(workflow):
    """
    Refuse a fine_level coarser than the natives, before removing anything.

    Regression: with natives at 13, fine_level=12 removed every derived tile
    as "a level this run does not build", built nothing, and exited 0 with
    an empty manifest and no overview Items.
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    overviews = workflow.layer / 'overviews'
    before = sorted(p.name for p in overviews.iterdir())
    published = workflow.published()
    output = workflow.refused(fine_level=12)
    assert 'fine_level=12' in output and '13_0_0.tif' in output
    assert sorted(p.name for p in overviews.iterdir()) == before
    assert workflow.published() == published
    assert workflow.log.read_text() == ''


def test_a_relative_tool_override_is_relative_to_where_snakemake_started(
        workflow, tmp_path):
    """
    Resolve a relative ``--config <tool>_tool=`` before ``workdir:``.

    Regression: overrides were resolved after ``workdir:`` had moved into
    ``.regenerate/``, so a relative path failed as "not an executable".
    """
    workflow.native('13_0_0.tif')
    fake = tmp_path / 'bin' / 'fake_build_depth_overview_parent'
    _, builds, _ = workflow.run(
        build_depth_overview_parent_tool=os.path.relpath(fake, tmp_path))
    assert builds == ['11_0_0', '12_0_0']


def test_a_regenerate_prunes_what_describes_nothing(workflow):
    """
    A native tile at a derived index, or vanished children, remove the tile.

    Regression: the per-parent DAG never removed a derived tile, so it stayed
    in overviews/, in coverage.json and in the Collection.
    """
    for name in ('13_0_0.tif', '13_2_2.tif'):
        workflow.native(name)
    workflow.run()
    overviews = workflow.layer / 'overviews'
    assert (overviews / '12_1_1.tif').is_file()

    # Compiled data lands at a derived tile's index: native wins.
    workflow.native('12_1_1.tif', value=3.0)
    _, builds, prunes = workflow.run()
    assert prunes == ['12_1_1']
    assert builds == ['11_0_0']
    assert not (overviews / '12_1_1.tif').exists()
    coverage = json.loads((overviews / 'coverage.json').read_text())
    assert [(lvl['level'], run['row'], run['col_min'])
            for lvl in coverage['levels'] for run in lvl['runs']] == [
        (11, 0, 0), (12, 0, 0)]

    # A derived tile's only child goes away (with its Item: the link step
    # owns both, and an Item whose tile is gone is refused by the index).
    (workflow.layer / '13_0_0.tif').unlink()
    (workflow.layer / 'depths-reviewed-surveyed-13_0_0.json').unlink()
    _, builds, prunes = workflow.run()
    assert prunes == ['12_0_0']
    assert builds == ['11_0_0']
    record = json.loads((overviews / '11_0_0.json').read_text())
    assert record['children'] == ['12_1_1.tif']


def test_the_catalog_and_indexes_are_this_layers(workflow):
    """
    Catalog the layer the DAG built, and index it from its Items.

    Regression: the catalog rule regenerated whatever tree the environment's
    store root named, and the index globbed only overviews/.
    """
    for name in ('13_0_0.tif', '13_0_1.tif'):
        workflow.native(name)
    output, _, _ = workflow.run()
    assert not workflow.elsewhere.exists()
    collection = json.loads((workflow.layer / 'collection.json').read_text())
    assert collection['summaries']['mws:levels'] == [11, 12, 13]
    assert (workflow.layer / 'depths-reviewed-surveyed-12_0_0.json').is_file()
    # One index per band schema, both written on any GDAL this repo runs on.
    assert (workflow.layer / 'index.gti.fgb').is_file()
    assert (workflow.layer / 'overviews' / 'index.gti.fgb').is_file()


def test_two_runs_over_one_layer_are_serialised(workflow, tmp_path):
    """
    A second run over a layer refuses while the first holds its lock.

    Regression: only Snakemake's per-working-directory lock existed, so two
    runs started from two directories interleaved over one layer.
    """
    import fcntl
    import subprocess
    workflow.native('13_0_0.tif')
    lock = workflow.layer / '.regenerate' / 'regenerate.lock'
    lock.parent.mkdir(exist_ok=True)
    with open(lock, 'a') as held:
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        elsewhere = tmp_path / 'another-cwd'
        elsewhere.mkdir()
        result = subprocess.run(
            ['snakemake', '-s', str(SNAKEMAKE_DIR / 'Snakefile'), '--cores',
             '1', '--config', f'layer_dir={workflow.layer}', 'fine_level=13',
             'min_level=11'],
            capture_output=True, text=True, cwd=str(elsewhere))
    assert result.returncode != 0
    assert 'another regenerate is running' in result.stdout + result.stderr
    assert not (workflow.layer / 'overviews').exists()


def test_a_dry_run_writes_nothing(workflow):
    """-n plans the DAG and writes no .fp, no tile and no Item."""
    workflow.native('13_0_0.tif')
    before = sorted(p.relative_to(workflow.layer)
                    for p in workflow.layer.rglob('*')
                    if '.regenerate' not in p.parts)
    output, builds, _ = workflow.run('--dry-run')
    assert builds == []
    assert 'fingerprint pre-step is skipped' in output
    after = sorted(p.relative_to(workflow.layer)
                   for p in workflow.layer.rglob('*')
                   if '.regenerate' not in p.parts)
    assert after == before


def test_the_workflow_requires_the_layer_directory(tmp_path):
    """No default layer, and a missing one is named."""
    pytest.importorskip('snakemake', reason='declared dependency; rosdep')
    import subprocess
    for config in ([], [f'layer_dir={tmp_path / "absent"}']):
        result = subprocess.run(
            ['snakemake', '-s', str(SNAKEMAKE_DIR / 'Snakefile'), '-n',
             '--cores', '1', *(['--config', *config] if config else [])],
            capture_output=True, text=True, cwd=str(tmp_path))
        assert result.returncode != 0
        assert 'layer_dir' in result.stdout + result.stderr
