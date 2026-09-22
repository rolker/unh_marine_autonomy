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
``mws_regenerate_catalog`` -- rewrite the Collections over existing Items.

Design section 9's replica rule, mechanically: **only changed Items are
written**. The Items themselves are produced by whichever tool built the
products; this rebuilds each cell's ``collection.json`` from what is on disk
and reports what actually changed, so an unchanged store leaves every file's
bytes and mtime alone and a replica sync moves nothing.
"""

from __future__ import annotations

import argparse
from typing import Optional, Sequence

from marine_world_store import layout
from marine_world_store.cli._common import (
    add_store_root_argument, resolved_root, run, stac_catalog,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_regenerate_catalog', description=__doc__.splitlines()[0])
    add_store_root_argument(parser)
    parser.add_argument(
        '--quantity', action='append', default=None,
        choices=[q.value for q in layout.Quantity],
        help='limit to one quantity (repeatable; default: all present)')
    parser.add_argument('--no-validate', action='store_true',
                        help='skip STAC validation of the Collections')
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Rebuild every present cell's Collection."""
    args = build_parser().parse_args(argv)
    # Acquired on the first cell that needs it: a store with nothing in
    # it regenerates nothing and should not require the writer to be
    # installed to say so.
    catalog = None
    root = resolved_root(args)
    quantities = [layout.Quantity(q) for q in (
        args.quantity or [q.value for q in layout.Quantity])]

    cells = 0
    changed = 0
    for quantity in quantities:
        for state in layout.State:
            for origin in layout.Origin:
                directory = layout.quantity_dir(
                    root.path, quantity, state, origin)
                if not directory.is_dir():
                    continue
                cells += 1
                catalog = catalog or stac_catalog()
                path, written, items = catalog.regenerate_collection(
                    directory,
                    quantity=quantity, state=state, origin=origin,
                    description=(
                        f'{quantity.value} / {state.value} / {origin.value}'),
                    validate=not args.no_validate)
                changed += 1 if written else 0
                print(f'{"wrote" if written else "unchanged"}: {path} '
                      f'({len(items)} item(s))')
    print(f'{cells} cell(s) present, {changed} collection(s) rewritten')
    if cells == 0:
        # Not an error: a store with no products yet is a legitimate state.
        print('nothing to regenerate: no quantity directories under this root')
    return 0


def console_main() -> int:
    """Entry point (``console_scripts``)."""
    return run(main)


if __name__ == '__main__':
    raise SystemExit(console_main())
