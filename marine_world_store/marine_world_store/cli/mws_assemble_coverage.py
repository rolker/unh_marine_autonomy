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
``mws_assemble_coverage`` -- one coverage manifest from the per-tile records.

The per-parent overview writer leaves a record beside each tile rather than
touching a shared ``coverage.json``, because a parallel DAG would race over
that one file. This is the single serialised step that turns those records into
the manifest uma-ADR-0013 D3 expects, run once after the DAG.

See :mod:`marine_world_store.overview_records`.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional, Sequence

from marine_world_store import layout, overview_records
from marine_world_store.cli._common import run


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_assemble_coverage', description=__doc__.splitlines()[1])
    parser.add_argument(
        'layer_dir', metavar='LAYER_DIR',
        help='a quantity layer; its overviews/ records are assembled')
    parser.add_argument(
        '--kind', default='derived',
        help='provenance label recorded in the document (default: derived)')
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Assemble a layer's overview coverage manifest."""
    args = build_parser().parse_args(argv)
    overviews = layout.overviews_dir(Path(args.layer_dir))
    if not overviews.is_dir():
        raise OSError(
            f'no overviews sidecar at {overviews}; nothing to assemble. Build '
            'the parents first (build_depth_overview_parent).')
    _, unrecorded = overview_records.manifest_records(overviews)
    path = overview_records.assemble(overviews, kind=args.kind)
    records, _ = overview_records.manifest_records(overviews)
    print(f'wrote {path}: {len(records)} tile(s)')
    if unrecorded:
        # Named: these carry the error the previous manifest gave them (or
        # none), not one a per-tile record vouches for.
        print(f'{len(unrecorded)} tile(s) have no per-tile record (batch-built, '
              'or interrupted before the record was written); their error is '
              'carried over from the previous manifest where it had one:')
        for level, row, col in unrecorded:
            print(f'  {layout.tile_filename(level, row, col)}')
    folds = overview_records.sigma_folds(overviews)
    if folds:
        # Printed even when there is only one: "undecided" is a statement about
        # design section 7 being open, and it should be visible on every run
        # rather than only when something disagrees.
        summary = ', '.join(f'{name}: {count}'
                            for name, count in sorted(folds.items()))
        print(f'sigma fold rules recorded -- {summary}')
    return 0


def console_main() -> int:
    """Entry point."""
    return run(main)
