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
``mws_refresh_fingerprints`` -- the regenerate DAG's pre-step.

Design section 9 makes fingerprints, not mtimes, the trigger for a regenerate;
Snakemake's DAG is mtime-driven. This reconciles them: it decides from each
tile's CONTENT whether it changed, then sets the tile's mtime to say so -- a
byte-identical rewrite goes back to its recorded mtime, a real change to a
native tile (even one copied in with an old mtime) is advanced to now, and a
DERIVED tile whose content is not the recorded content is removed so the DAG
rebuilds it. ``--record`` records tiles
the DAG has just built, with the mtime the build gave them. See
:mod:`marine_world_store.fingerprint_sidecar` for what the ``.fp`` sidecar is
and, just as importantly, what it is **not** (it is a content hash, not section
9's input fingerprint).

Run it over a layer before the overview rules, and ``--record`` each tile a
rule builds, which is what the Snakefile does.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional, Sequence

from marine_world_store import fingerprint_sidecar
from marine_world_store.cli._common import run


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_refresh_fingerprints',
        description=__doc__.splitlines()[1])
    parser.add_argument(
        'layer_dir', metavar='LAYER_DIR', nargs='?',
        help='a quantity layer; its tiles and its overviews/ are refreshed')
    parser.add_argument(
        '--record', metavar='TILE', action='append', default=[],
        help=('record TILE as just built (its content and current mtime), '
              'instead of refreshing a layer; repeatable'))
    parser.add_argument(
        '--keep-orphans', action='store_true',
        help=('keep a .fp whose tile is gone (default: remove it -- it '
              'asserts a fingerprint nothing can check)'))
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Refresh a layer's fingerprint sidecars."""
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.record:
        if args.layer_dir is not None:
            parser.error('--record takes tiles, not a LAYER_DIR')
        for tile in args.record:
            fingerprint_sidecar.record_tile(tile)
        return 0
    if args.layer_dir is None:
        parser.error('LAYER_DIR is required (or --record TILE)')
    layer_dir = Path(args.layer_dir)
    if not layer_dir.is_dir():
        raise OSError(f'not a directory: {layer_dir}')
    for scope, report in fingerprint_sidecar.refresh_layer(layer_dir).items():
        print(f'{scope}: {report.unchanged} unchanged (mtime reset), '
              f'{report.changed} changed, {report.created} new, '
              f'{report.orphans_removed} orphan sidecar(s) removed')
        for unreadable in report.unreadable:
            # Named, not counted: an unreadable sidecar was treated as absent,
            # so the tile it belongs to will look changed to the DAG once.
            print(f'  unreadable sidecar, rewritten: {unreadable}')
        for removed in report.removed:
            # A derived tile not built from what is there now: removed with
            # its record and sidecar, so the DAG rebuilds it.
            print(f'  derived tile not the recorded content, removed: '
                  f'{removed}')
        for link in report.symlinks_skipped:
            # Never reconciled: utime would reach through the link.
            print(f'  symbolic link, left alone: {link}')
    return 0


def console_main() -> int:
    """Entry point."""
    return run(main)
