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
The sigma-fold candidate measurement (uma#397, design section 7).

Synthetic rasters only: the arithmetic under test is in
:func:`marine_world_store.sigma_fold_measure.measure_arrays`, which takes arrays,
so nothing here needs GDAL, a GeoTIFF fixture or a tile on disk. The real
Massabesic subset reaches the measurement as CLI arguments -- never as a path in
this repo.

What these tests protect is that the report can be TRUSTED as evidence: that
each candidate is carried forward in its own right across fold steps, that the
truth it is scored against is the native population and not a fold of it, and
that a rule with no sigma to work from reports nodata rather than zero.
"""

import math

from marine_world_store import sigma_fold_measure as sfm

import numpy as np

import pytest


def _uniform(shape, depth, sigma):
    return (np.full(shape, depth, dtype=float),
            np.full(shape, sigma, dtype=float))


def test_uniform_depths_have_no_spread_and_every_rule_covers():
    """A perfectly flat patch: the truth is zero, so nothing can undercover."""
    depth, sigma = _uniform((8, 8), -12.0, 0.25)
    [first, *_rest] = sfm.measure_arrays([(depth, sigma, 13)], steps=1)
    assert first.mean_true_spread == pytest.approx(0.0)
    for rule in sfm.CANDIDATES:
        assert first.coverage[rule] == 1.0
        # No spread between the children, so pooled degenerates to the child
        # sigma itself -- which is the only honest answer here.
        assert first.mean_sigma[rule] == pytest.approx(0.25)


def test_pooled_sees_the_spread_between_children_and_the_others_do_not():
    """
    The finding the whole measurement exists to expose.

    A parent over cells that disagree by metres, each with a centimetre sigma,
    has a true spread of metres. `max_child` and `mean_child` can only ever
    report the centimetres; `pooled` is the one candidate that can see the
    disagreement at all.
    """
    depth = np.array([[-10.0, -2.0], [-2.0, -10.0]])
    sigma = np.full((2, 2), 0.1)
    [first] = sfm.measure_arrays([(depth, sigma, 13)], steps=1)
    assert first.cells == 1
    assert first.mean_true_spread == pytest.approx(4.0)
    assert first.mean_sigma['max_child'] == pytest.approx(0.1)
    assert first.mean_sigma['mean_child'] == pytest.approx(0.1)
    assert first.mean_sigma['pooled'] == pytest.approx(math.sqrt(0.01 + 16.0))
    assert first.coverage['pooled'] == 1.0
    assert first.coverage['max_child'] == 0.0
    assert first.coverage['mean_child'] == 0.0


def test_truth_is_the_native_population_not_a_fold_of_it():
    """
    Score against the native population, not against a fold of it.

    Step two's truth counts every native cell under the parent, not the four
    cells it folded. A measurement that re-derived the spread from the fold
    would be scoring the candidates against themselves.
    """
    depth = np.array([[-1.0, -2.0, -3.0, -4.0],
                      [-5.0, -6.0, -7.0, -8.0],
                      [-9.0, -10.0, -11.0, -12.0],
                      [-13.0, -14.0, -15.0, -16.0]])
    sigma = np.full((4, 4), 0.1)
    measurements = sfm.measure_arrays([(depth, sigma, 13)], steps=2)
    assert [m.step for m in measurements] == [1, 2]
    second = measurements[1]
    assert second.cells == 1
    expected = float(np.std(depth))
    assert second.mean_true_spread == pytest.approx(expected)


def test_each_candidate_is_carried_forward_in_its_own_right():
    """
    Fold each candidate's own sigma at every step.

    Step two must fold the sigmas step one would have WRITTEN under each rule.
    Sharing one sigma across candidates would make the second step's comparison
    meaningless, and it is the multi-step behaviour the decision turns on.
    """
    depth = np.array([[-1.0, -2.0, -30.0, -4.0],
                      [-5.0, -6.0, -7.0, -8.0],
                      [-9.0, -10.0, -11.0, -12.0],
                      [-13.0, -14.0, -15.0, -60.0]])
    sigma = np.full((4, 4), 0.2)
    _first, second = sfm.measure_arrays([(depth, sigma, 13)], steps=2)
    # max_child of max_child is still 0.2 -- a per-child statistic cannot grow.
    assert second.mean_sigma['max_child'] == pytest.approx(0.2)
    assert second.mean_sigma['mean_child'] == pytest.approx(0.2)
    # pooled compounds: it already saw the spread within each child at step one
    # and now adds the spread between the children.
    assert second.mean_sigma['pooled'] > second.mean_sigma['max_child']


def test_no_sigma_anywhere_reports_nodata_not_zero():
    """Zero uncertainty is the most dangerous number this band could hold."""
    depth = np.array([[-1.0, -2.0], [-3.0, -4.0]])
    sigma = np.full((2, 2), np.nan)
    [first] = sfm.measure_arrays([(depth, sigma, 13)], steps=1)
    for rule in sfm.CANDIDATES:
        assert math.isnan(first.mean_sigma[rule]), rule
        # Nothing was comparable, so coverage is not a claim either way.
        assert math.isnan(first.coverage[rule]) or first.coverage[rule] == 0.0


def test_a_missing_child_sigma_still_contributes_its_mean_to_pooled():
    """
    Keep a sigma-less child in the pooled spread.

    Dropping it entirely would understate the spread the band exists to report
    -- its within-child variance is zero, its distance from the pooled mean is
    not.
    """
    depth = np.array([[-10.0, -2.0], [-2.0, -10.0]])
    sigma = np.array([[np.nan, 0.1], [0.1, np.nan]])
    [first] = sfm.measure_arrays([(depth, sigma, 13)], steps=1)
    assert first.mean_sigma['pooled'] > 3.9
    assert first.coverage['pooled'] == 1.0


def test_nodata_depth_cells_do_not_contribute():
    """Skip no-data cells: a NaN depth is the store's sentinel."""
    depth = np.array([[-4.0, np.nan], [np.nan, np.nan]])
    sigma = np.array([[0.5, np.nan], [np.nan, np.nan]])
    [first] = sfm.measure_arrays([(depth, sigma, 13)], steps=1)
    assert first.cells == 1
    # One native cell under the parent: there is no spread to cover, so the cell
    # is counted but not scored. Scoring it would mark every rule as covering.
    assert first.comparable_cells == 0
    assert first.mean_sigma['max_child'] == pytest.approx(0.5)


def test_an_all_nodata_patch_measures_nothing_rather_than_reporting_zeros():
    """Measure nothing, rather than report zeros, over an empty patch."""
    depth = np.full((4, 4), np.nan)
    sigma = np.full((4, 4), np.nan)
    assert sfm.measure_arrays([(depth, sigma, 13)], steps=2) == []


def test_levels_are_labelled_from_the_tiles_native_level():
    """Label each step with the parent level it produced."""
    depth, sigma = _uniform((8, 8), -12.0, 0.25)
    measurements = sfm.measure_arrays([(depth, sigma, 13)], steps=3)
    assert [m.level for m in measurements] == [12, 11, 10]


def test_mixed_native_levels_report_steps_not_levels():
    """
    Report a step number when the tiles share no native level.

    Two tiles at different native levels have no common parent level, so the
    report must say 'step 1', not invent one. Labelling them with either level
    would be a claim about ground the other tile does not cover.
    """
    depth, sigma = _uniform((8, 8), -12.0, 0.25)
    measurements = sfm.measure_arrays(
        [(depth, sigma, 13), (depth, sigma, 12)], steps=1)
    assert measurements[0].level is None
    assert measurements[0].cells == 32


def test_measurement_stops_when_the_raster_runs_out_of_resolution():
    """Stop at the last measurable step instead of inventing cells."""
    depth, sigma = _uniform((2, 2), -12.0, 0.25)
    measurements = sfm.measure_arrays([(depth, sigma, 13)], steps=4)
    assert [m.step for m in measurements] == [1]


def test_an_odd_grid_ends_the_measurement_rather_than_crashing():
    """
    960 -> 480 -> ... -> 15 is odd: no 2x2 parent, so the measurement stops.

    Regression: the reshape in _blocks raised an unexplained ValueError.
    """
    depth, sigma = _uniform((60, 60), -12.0, 0.25)
    measurements = sfm.measure_arrays([(depth, sigma, 13)], steps=4)
    # 60 -> 30 -> 15: two steps, then 15 is odd.
    assert [m.step for m in measurements] == [1, 2]
    depth, sigma = _uniform((6, 4), -12.0, 0.25)
    measurements = sfm.measure_arrays([(depth, sigma, 13)], steps=3)
    assert [m.step for m in measurements] == [1]


def test_a_tile_that_is_not_two_band_is_refused(tmp_path):
    """
    A 4-band overview is not read as {depth, sigma}.

    Regression: the check was `< 2`, so a 4-band tile read MIN as depth and
    MEAN as sigma.
    """
    gdal = pytest.importorskip('osgeo.gdal')
    gdal.UseExceptions()
    for bands in (1, 4):
        path = tmp_path / f'12_0_{bands}.tif'
        gdal.GetDriverByName('GTiff').Create(
            str(path), 4, 4, bands, gdal.GDT_Float64).FlushCache()
        with pytest.raises(ValueError, match='2-band'):
            sfm.read_depth_tile(path)


def test_rejects_mismatched_or_non_2d_rasters():
    """Refuse inputs that are not a matched pair of 2-D rasters."""
    with pytest.raises(ValueError):
        sfm.measure_arrays([(np.zeros((4, 4)), np.zeros((4, 5)), 13)])
    with pytest.raises(ValueError):
        sfm.measure_arrays([(np.zeros(4), np.zeros(4), 13)])
    with pytest.raises(ValueError):
        sfm.measure_arrays([])
    with pytest.raises(ValueError):
        sfm.measure_arrays([(np.zeros((4, 4)), np.zeros((4, 4)), 13)], steps=0)


def test_report_is_a_markdown_table_that_recommends_nothing():
    """
    Hand the numbers over without picking a rule.

    The report hands numbers to a person. A 'suggested rule' line would be
    section 7's decision taken by the measurement instead -- exactly what the
    operator asked not to happen.
    """
    depth = np.array([[-10.0, -2.0], [-2.0, -10.0]])
    sigma = np.full((2, 2), 0.1)
    report = sfm.format_report(sfm.measure_arrays([(depth, sigma, 13)], steps=1))
    assert report.startswith('# Sigma-fold candidate measurement')
    assert '| step | level |' in report
    for rule in sfm.CANDIDATES:
        assert f'covers {rule}' in report
    lowered = report.lower()
    for forbidden in ('recommend', 'we should use', 'chosen rule is',
                      'the winner'):
        assert forbidden not in lowered
    assert 'undecided' in report


def test_expand_tile_arguments_takes_files_and_directories(tmp_path):
    """Expand a directory to its sorted ``*.tif``, and keep loose files."""
    (tmp_path / 'b.tif').write_bytes(b'')
    (tmp_path / 'a.tif').write_bytes(b'')
    (tmp_path / 'notes.txt').write_text('ignored')
    loose = tmp_path / 'elsewhere' / 'loose.tif'
    loose.parent.mkdir()
    loose.write_bytes(b'')
    found = sfm.expand_tile_arguments([str(tmp_path), str(loose)])
    # Sorted within a directory, so two runs report the same numbers in the
    # same order whatever the filesystem's iteration order is.
    assert [p.name for p in found] == ['a.tif', 'b.tif', 'loose.tif']


def test_cli_refuses_arguments_that_name_no_tile(tmp_path, capsys):
    """Say so when the arguments name no tile, instead of measuring nothing."""
    from marine_world_store.cli import mws_measure_sigma_fold as cli
    assert cli.console_main.__doc__
    with pytest.raises(ValueError):
        cli.main([str(tmp_path)])
    with pytest.raises(OSError):
        cli.main([str(tmp_path / 'absent.tif')])
