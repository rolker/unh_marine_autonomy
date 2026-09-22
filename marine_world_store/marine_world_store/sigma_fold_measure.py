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
Measure the candidate sigma-fold rules against the truth they claim to summarise.

``docs/world_store_design.md`` section 7 leaves the overview tile's sigma band's
fold rule **open** (Roland, 2026-09-22: "this seems like something that should be
thought about much more").  Rev 2's "mean and max of the children" named two
numbers without saying how they combine into one stored value, so it was never a
decision.  This module produces the evidence the decision is taken from, in the
style of spine decision 2's own ``fold_measure``: over a set of native depth
tiles it reports, per fold step, how the candidates differ and how often each
one's sigma **covers the true spread of the native cells under the parent**.

It decides nothing.  No rule is recommended here and no sigma band is written by
anything this module touches; the writers keep emitting nodata with
``sigma_fold: undecided`` until the rule is chosen.

The candidates, exactly as section 7 lists them:

``pooled``
    Within-child variance plus the spread of the child means, count-weighted:
    ``sigma^2 = sum(n_i (sigma_i^2 + (mu_i - mu)^2)) / sum(n_i)``.
``max_child``
    The largest child sigma.
``mean_child``
    The count-weighted mean of the child sigmas.
``mean and max``
    Rev 2's literal phrasing, reported as the two numbers it is -- the
    ``mean_child`` and ``max_child`` columns side by side -- rather than as a
    third rule, because it never said how they combine.

Each candidate is carried forward **in its own right** across fold steps: step
two folds the sigmas step one would have written under that rule, not a shared
sigma.  That is the only way a multi-step comparison says anything about what a
pyramid built under each rule would actually hold.

**The truth** is the population standard deviation of the NATIVE depth cells
beneath a parent cell, accumulated exactly (count, sum, sum of squares) through
every fold step.  A rule *covers* a parent cell when its sigma is at least that
spread.  A rule that covers rarely is claiming a tighter uncertainty than the
data supports, which is the direction that matters for a navigation view.

**One approximation, stated.** Cells are aggregated in aligned 2x2 blocks within
each tile rather than through the GGGS geographic parent mapping the writers
use.  In the non-polar survey envelope the two agree cell for cell away from a
tile's edges, and every statistic here is per-parent-cell, so an edge cell
assigned to a neighbouring parent changes no candidate's relationship to the
truth.  A measurement is not a writer: the approximation buys a module that
needs no GGGS bindings and runs over whatever tiles it is handed.

Inputs are **paths given by the caller** -- the real Massabesic subset tiles are
CLI arguments, never a literal in this repo, and the automated test uses
synthetic arrays.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import math
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple, Union

import numpy as np

PathLike = Union[str, Path]

#: The candidate rules, in the order the report prints them.
CANDIDATES: Tuple[str, ...] = ('pooled', 'max_child', 'mean_child')

#: How many fold steps a run measures unless the caller says otherwise. Three is
#: what spine decision 2's own Massabesic measurement used.
DEFAULT_STEPS = 3


@dataclass
class LevelMeasurement:
    """What one fold step looked like, pooled over every tile measured."""

    #: 1 for the first fold above the native level, 2 for the next, ...
    step: int
    #: The parent level, when every tile measured shares one native level.
    level: Optional[int] = None
    #: Parent cells that had at least one contributing native cell.
    cells: int = 0
    #: Parent cells whose native population is >= 2, i.e. where a spread exists
    #: to be covered at all. Coverage fractions are taken over these.
    comparable_cells: int = 0
    #: Mean sigma each candidate would have written.
    mean_sigma: Dict[str, float] = field(default_factory=dict)
    #: Mean of the true native spread under a parent cell.
    mean_true_spread: float = float('nan')
    #: Fraction of comparable cells where the candidate's sigma >= true spread.
    coverage: Dict[str, float] = field(default_factory=dict)
    #: Mean ratio candidate_sigma / true_spread over comparable cells.
    mean_ratio: Dict[str, float] = field(default_factory=dict)


@dataclass
class _State:
    """Per-cell accumulators carried from one fold step to the next."""

    n: np.ndarray      # native cells summarised by this cell
    s: np.ndarray      # sum of those native depths
    ss: np.ndarray     # sum of their squares
    mean: np.ndarray   # the MEAN band this cell would hold
    sigma: Dict[str, np.ndarray]   # the sigma band, per candidate rule


def _initial_state(depth: np.ndarray, sigma: np.ndarray) -> _State:
    """Promote a native ``{depth, sigma}`` raster to per-cell accumulators."""
    valid = np.isfinite(depth)
    n = valid.astype(np.float64)
    d = np.where(valid, depth, 0.0).astype(np.float64)
    # A native cell is a one-cell summary of itself, which is the same promotion
    # the C++ fold applies (detail::promoteNativeDepthCell).
    return _State(
        n=n,
        s=d,
        ss=d * d,
        mean=np.where(valid, depth, np.nan).astype(np.float64),
        sigma={rule: np.where(valid, sigma, np.nan).astype(np.float64)
               for rule in CANDIDATES},
    )


def _blocks(array: np.ndarray) -> np.ndarray:
    """Reshape ``(H, W)`` into ``(H/2, W/2, 4)`` -- one row per parent cell."""
    height, width = array.shape
    return (array.reshape(height // 2, 2, width // 2, 2)
            .transpose(0, 2, 1, 3)
            .reshape(height // 2, width // 2, 4))


def _weighted(values: np.ndarray, weights: np.ndarray) -> np.ndarray:
    """Count-weighted mean over the last axis, NaN where nothing contributes."""
    usable = np.isfinite(values) & (weights > 0)
    w = np.where(usable, weights, 0.0)
    total = w.sum(axis=-1)
    numerator = np.where(usable, values, 0.0) * w
    with np.errstate(invalid='ignore', divide='ignore'):
        out = numerator.sum(axis=-1) / total
    return np.where(total > 0, out, np.nan)


def _fold(state: _State) -> _State:
    """One fold step: 2x2 parent cells from the current cells."""
    n = _blocks(state.n)
    s = _blocks(state.s)
    ss = _blocks(state.ss)
    mean = _blocks(state.mean)
    contributes = n > 0

    parent_n = n.sum(axis=-1)
    parent_s = s.sum(axis=-1)
    parent_ss = ss.sum(axis=-1)
    with np.errstate(invalid='ignore', divide='ignore'):
        parent_mean = np.where(parent_n > 0, parent_s / parent_n, np.nan)

    sigma: Dict[str, np.ndarray] = {}
    for rule in CANDIDATES:
        child_sigma = _blocks(state.sigma[rule])
        usable = contributes & np.isfinite(child_sigma)
        any_sigma = usable.any(axis=-1)
        if rule == 'max_child':
            folded = np.where(usable, child_sigma, -np.inf).max(axis=-1)
        elif rule == 'mean_child':
            folded = _weighted(child_sigma, np.where(contributes, n, 0.0))
        else:   # pooled
            # A child with no sigma still contributes its mean's distance from
            # the pooled mean; dropping it would understate the spread the band
            # exists to report. Its within-child variance counts as zero.
            within = np.where(usable, child_sigma, 0.0) ** 2
            between = np.where(
                contributes, (mean - parent_mean[..., None]) ** 2, 0.0)
            weights = np.where(contributes, n, 0.0)
            accum = (weights * (within + between)).sum(axis=-1)
            with np.errstate(invalid='ignore', divide='ignore'):
                folded = np.sqrt(
                    np.where(parent_n > 0, accum / parent_n, np.nan))
        # No contributor carried a sigma at all: nodata, never zero. Zero
        # uncertainty is the most dangerous number this band could hold.
        sigma[rule] = np.where(any_sigma, folded, np.nan)

    return _State(n=parent_n, s=parent_s, ss=parent_ss, mean=parent_mean,
                  sigma=sigma)


def _true_spread(state: _State) -> np.ndarray:
    """Compute the population sigma of the native depths under each cell."""
    with np.errstate(invalid='ignore', divide='ignore'):
        variance = state.ss / state.n - (state.s / state.n) ** 2
    # Catastrophic cancellation can put an exactly-uniform population a hair
    # below zero; clamp rather than emit a NaN that would read as "no truth".
    variance = np.where(state.n > 0, np.maximum(variance, 0.0), np.nan)
    return np.sqrt(variance)


@dataclass
class _Accumulator:
    """Running totals for one fold step across every tile measured."""

    cells: int = 0
    comparable: int = 0
    sigma_sum: Dict[str, float] = field(
        default_factory=lambda: {rule: 0.0 for rule in CANDIDATES})
    sigma_count: Dict[str, int] = field(
        default_factory=lambda: {rule: 0 for rule in CANDIDATES})
    covered: Dict[str, int] = field(
        default_factory=lambda: {rule: 0 for rule in CANDIDATES})
    ratio_sum: Dict[str, float] = field(
        default_factory=lambda: {rule: 0.0 for rule in CANDIDATES})
    ratio_count: Dict[str, int] = field(
        default_factory=lambda: {rule: 0 for rule in CANDIDATES})
    spread_sum: float = 0.0
    spread_count: int = 0
    levels: set = field(default_factory=set)


def _accumulate(acc: _Accumulator, state: _State) -> None:
    populated = state.n > 0
    acc.cells += int(populated.sum())
    spread = _true_spread(state)
    # A parent over a single native cell has no spread to cover -- including it
    # would score every rule as covering, which says nothing.
    comparable = populated & (state.n >= 2)
    acc.comparable += int(comparable.sum())
    acc.spread_sum += float(np.nansum(np.where(comparable, spread, 0.0)))
    acc.spread_count += int(comparable.sum())
    for rule in CANDIDATES:
        sig = state.sigma[rule]
        have = populated & np.isfinite(sig)
        acc.sigma_sum[rule] += float(np.where(have, sig, 0.0).sum())
        acc.sigma_count[rule] += int(have.sum())
        scored = comparable & np.isfinite(sig) & np.isfinite(spread)
        acc.covered[rule] += int((scored & (sig >= spread)).sum())
        # The ratio is only meaningful where a spread exists to divide by.
        ratioable = scored & (spread > 0)
        with np.errstate(invalid='ignore', divide='ignore'):
            ratio = np.where(ratioable, sig / np.where(spread > 0, spread, 1.0),
                             0.0)
        acc.ratio_sum[rule] += float(ratio.sum())
        acc.ratio_count[rule] += int(ratioable.sum())


def _finish(step: int, acc: _Accumulator) -> LevelMeasurement:
    def _mean(total: float, count: int) -> float:
        return total / count if count else float('nan')

    level = None
    if len(acc.levels) == 1:
        level = next(iter(acc.levels)) - step
    return LevelMeasurement(
        step=step,
        level=level,
        cells=acc.cells,
        comparable_cells=acc.comparable,
        mean_sigma={rule: _mean(acc.sigma_sum[rule], acc.sigma_count[rule])
                    for rule in CANDIDATES},
        mean_true_spread=_mean(acc.spread_sum, acc.spread_count),
        coverage={rule: (acc.covered[rule] / acc.comparable
                         if acc.comparable else float('nan'))
                  for rule in CANDIDATES},
        mean_ratio={rule: _mean(acc.ratio_sum[rule], acc.ratio_count[rule])
                    for rule in CANDIDATES},
    )


def measure_arrays(
    rasters: Iterable[Tuple[np.ndarray, np.ndarray, Optional[int]]],
    steps: int = DEFAULT_STEPS,
) -> List[LevelMeasurement]:
    """
    Measure the candidates over ``(depth, sigma, native_level)`` rasters.

    The array-level entry point, so the automated test needs no GeoTIFF fixture
    and no GDAL: the arithmetic under measurement is here, and
    :func:`measure_tiles` only adds reading.

    :param rasters: each a 2-D depth raster, its matching sigma raster, and the
        native GGGS level the tile sits at (``None`` when unknown -- the report
        then labels steps rather than levels).
    :param steps: how many fold steps to measure.
    :returns: one :class:`LevelMeasurement` per step, step 1 first.
    """
    if steps < 1:
        raise ValueError('steps must be >= 1')
    accumulators = [_Accumulator() for _ in range(steps)]
    measured_any = False
    for depth, sigma, level in rasters:
        depth = np.asarray(depth, dtype=np.float64)
        sigma = np.asarray(sigma, dtype=np.float64)
        if depth.shape != sigma.shape or depth.ndim != 2:
            raise ValueError(
                'depth and sigma must be 2-D rasters of the same shape, got '
                f'{depth.shape} and {sigma.shape}')
        state = _initial_state(depth, sigma)
        measured_any = True
        for index in range(steps):
            if state.n.shape[0] < 2 or state.n.shape[1] < 2:
                # Out of resolution: report the steps that were measurable
                # rather than padding the table with cells that do not exist.
                break
            state = _fold(state)
            if level is not None:
                accumulators[index].levels.add(int(level))
            _accumulate(accumulators[index], state)
    if not measured_any:
        raise ValueError('no rasters to measure')
    return [_finish(index + 1, acc) for index, acc in enumerate(accumulators)
            if acc.cells > 0]


def read_depth_tile(path: PathLike) -> Tuple[np.ndarray, np.ndarray, Optional[int]]:
    """
    Read a 2-band native depth tile as ``(depth, sigma, level)``.

    GDAL is imported here rather than at module import so that
    :func:`measure_arrays` -- and its tests -- run on a host without it.
    """
    from osgeo import gdal   # noqa: PLC0415  (deliberate: see docstring)

    gdal.UseExceptions()
    dataset = gdal.Open(str(path))
    if dataset is None:
        raise OSError(f'cannot open {path}')
    if dataset.RasterCount < 2:
        raise ValueError(
            f'{path} has {dataset.RasterCount} band(s); a native depth tile is '
            'the 2-band {depth, sigma} pair. A 4-band overview tile is not an '
            'input here: the measurement reads the native truth, not a fold of '
            'it.')
    depth = dataset.GetRasterBand(1).ReadAsArray().astype(np.float64)
    sigma = dataset.GetRasterBand(2).ReadAsArray().astype(np.float64)
    level = None
    stem = Path(path).name.split('_')
    if stem and stem[0].isdigit():
        level = int(stem[0])
    return depth, sigma, level


def measure_tiles(
    paths: Sequence[PathLike], steps: int = DEFAULT_STEPS,
) -> List[LevelMeasurement]:
    """Measure the candidates over native depth tiles named by ``paths``."""
    return measure_arrays(
        (read_depth_tile(path) for path in paths), steps=steps)


def expand_tile_arguments(arguments: Sequence[PathLike]) -> List[Path]:
    """
    Turn CLI arguments into tile paths: a directory yields its ``*.tif``.

    Sorted, so two runs over one directory report the same numbers in the same
    order whatever the filesystem's iteration order happens to be.
    """
    paths: List[Path] = []
    for argument in arguments:
        path = Path(argument)
        if path.is_dir():
            paths.extend(sorted(path.glob('*.tif')))
        else:
            paths.append(path)
    return paths


def _cell(value: float) -> str:
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return 'n/a'
    return f'{value:.4g}'


def format_report(measurements: Sequence[LevelMeasurement]) -> str:
    """
    Render the measurement as a markdown report.

    Deliberately ends with no recommendation: section 7's rule is decided by a
    person reading these numbers, and a "suggested rule" line here would be that
    decision taken by the measurement instead.
    """
    lines: List[str] = []
    lines.append('# Sigma-fold candidate measurement (uma#397, '
                 'world_store_design.md section 7)')
    lines.append('')
    lines.append('Candidates measured over native depth tiles. **Truth** is the '
                 'population')
    lines.append('standard deviation of the native depth cells under a parent '
                 'cell. A rule')
    lines.append('*covers* a cell when its sigma is at least that spread.')
    lines.append('')
    lines.append('Rev 2\'s literal "mean and max of the children" is the '
                 '`mean_child` and')
    lines.append('`max_child` columns read together -- two numbers, which is '
                 'exactly why it')
    lines.append('was never a decision.')
    lines.append('')
    header = ('| step | level | parent cells | with a spread | mean true '
              'spread (m) | ' +
              ' | '.join(f'mean sigma {rule} (m)' for rule in CANDIDATES) +
              ' | ' +
              ' | '.join(f'covers {rule}' for rule in CANDIDATES) +
              ' | ' +
              ' | '.join(f'sigma/spread {rule}' for rule in CANDIDATES) +
              ' |')
    lines.append(header)
    lines.append('|' + '---|' * (5 + 3 * len(CANDIDATES)))
    for m in measurements:
        row = [str(m.step),
               'n/a' if m.level is None else str(m.level),
               str(m.cells),
               str(m.comparable_cells),
               _cell(m.mean_true_spread)]
        row += [_cell(m.mean_sigma[rule]) for rule in CANDIDATES]
        row += [_cell(m.coverage[rule]) for rule in CANDIDATES]
        row += [_cell(m.mean_ratio[rule]) for rule in CANDIDATES]
        lines.append('| ' + ' | '.join(row) + ' |')
    lines.append('')
    lines.append('No rule is chosen here. Until one is, the writers emit the '
                 'sigma band as')
    lines.append('nodata and record `sigma_fold: undecided`, so the decision '
                 'when it comes is')
    lines.append('a new fingerprint rather than a migration.')
    return '\n'.join(lines) + '\n'
