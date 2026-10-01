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
``mws_measure_sigma_fold`` -- evidence for section 7's open sigma-fold rule.

Reads NATIVE depth tiles named on the command line (files, or directories whose
``*.tif`` are taken) and prints how the candidate rules would differ, per fold
step, against the true spread of the native cells under each parent cell. The
module docstring of :mod:`marine_world_store.sigma_fold_measure` states what is
measured and the one approximation it makes.

It writes nothing and decides nothing: the numbers go to the operator, the
design thinking happens there, and the chosen rule goes into
``docs/world_store_design.md`` section 7 as a spine-2 refinement. Until then
the overview writers keep emitting the sigma band as nodata with
``sigma_fold: undecided``.

Tile paths are **arguments**, never literals in this repo -- the Massabesic
subset lives on the operator's disk and the store root is configurable, so
naming a path here would be the hard-coded path the guard test exists to
forbid.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional, Sequence

from marine_world_store import sigma_fold_measure
from marine_world_store.cli._common import run


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_measure_sigma_fold',
        description=__doc__.splitlines()[1])
    parser.add_argument(
        'tiles', nargs='+', metavar='TILE_OR_DIR',
        help='native 2-band depth tiles, or directories holding them')
    parser.add_argument(
        '--steps', type=int, default=sigma_fold_measure.DEFAULT_STEPS,
        help=('how many fold steps to measure (default: '
              f'{sigma_fold_measure.DEFAULT_STEPS}, what spine decision 2 '
              'used for its own Massabesic measurement)'))
    parser.add_argument(
        '--output', default=None, metavar='FILE',
        help='write the report here instead of stdout')
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Measure the candidates and print the report."""
    args = build_parser().parse_args(argv)
    if args.steps < 1:
        raise ValueError('--steps must be at least 1')
    paths = sigma_fold_measure.expand_tile_arguments(args.tiles)
    if not paths:
        raise ValueError(
            'no tiles to measure: the arguments named no file and no directory '
            'holding a *.tif')
    missing = [str(p) for p in paths if not p.is_file()]
    if missing:
        raise OSError('no such tile: ' + ', '.join(missing))
    report = sigma_fold_measure.format_report(
        sigma_fold_measure.measure_tiles(paths, steps=args.steps))
    if args.output:
        Path(args.output).write_text(report)
        print(f'wrote {args.output}')
    else:
        print(report, end='')
    return 0


def console_main() -> int:
    """Entry point."""
    return run(main)
