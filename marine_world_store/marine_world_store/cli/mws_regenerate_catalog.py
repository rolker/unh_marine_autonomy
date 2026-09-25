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
written**. A native tile's Item is produced by whichever tool built the
product; the derived OVERVIEW tiles' Items are built here, from the per-tile
records the per-parent writer leaves (:mod:`marine_world_store.
overview_items`), and an overview Item whose tile is gone is removed. Each
cell's ``collection.json`` is then rebuilt from what is on disk, and what
actually changed is reported, so an unchanged store leaves every file's bytes
and mtime alone and a replica sync moves nothing.

Two scopes: every cell under the store root (the default), or exactly one
quantity layer with ``--layer-dir`` -- what the regenerate DAG runs, so that it
catalogs the layer it just built rather than whatever tree the environment's
store root names.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional, Sequence, Tuple

from marine_world_store import layout, overview_items
from marine_world_store.cli._common import (
    add_store_root_argument, resolved_root, run, stac_catalog,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_regenerate_catalog', description=__doc__.splitlines()[1])
    add_store_root_argument(parser)
    parser.add_argument(
        '--quantity', action='append', default=None,
        choices=[q.value for q in layout.Quantity],
        help='limit to one quantity (repeatable; default: all present)')
    parser.add_argument(
        '--layer-dir', default=None, metavar='DIR',
        help=('regenerate exactly this quantity layer '
              '(<root>/<quantity>/<state>/<origin>/) instead of every cell '
              'under the store root; --store-root and --quantity do not '
              'apply'))
    parser.add_argument('--no-validate', action='store_true',
                        help='skip STAC validation of the Collections')
    return parser


def layer_cell(
        layer_dir: Path) -> Tuple[layout.Quantity, layout.State, layout.Origin]:
    """
    Read ``(quantity, state, origin)`` off a layer path's last three parts.

    :raises layout.LayoutError: when the path is not
        ``<root>/<quantity>/<state>/<origin>/`` -- a typo'd layer is refused
        by name, never catalogued as a cell nobody asked for.
    """
    parts = layer_dir.resolve().parts
    if len(parts) < 4:
        raise layout.LayoutError(
            f'{layer_dir}: not a <root>/<quantity>/<state>/<origin>/ layer')
    quantity, state, origin = parts[-3:]
    try:
        return (layout.Quantity(quantity), layout.State(state),
                layout.Origin(origin))
    except ValueError as exc:
        raise layout.LayoutError(
            f'{layer_dir}: not a <root>/<quantity>/<state>/<origin>/ layer '
            f'({exc})') from exc


def refuse_orphan_native_items(directory: Path, item_files) -> None:
    """
    Refuse a NATIVE tile's Item whose tile is gone, before anything is written.

    Native tiles and their Items are the link step's, not this tool's, so it
    does not remove one -- but it must not publish a Collection listing a tile
    that is not there, and the tile indexes would refuse it next. Naming the
    Item and the remedy here is what an operator can act on; the first
    version rewrote the Collection and let the index fail with "regenerate
    the catalog", the step that had just run.

    :raises OSError: naming every such Item file.
    """
    orphans = []
    for path, item in item_files:
        if overview_items.is_overview_item(item):
            continue
        href = ((item.get('assets') or {}).get('data') or {}).get('href')
        if href and not (directory / href).is_file():
            orphans.append(f'{path} (records {href})')
    if orphans:
        raise OSError(
            'native tile(s) gone but their Item(s) remain: '
            + '; '.join(orphans) + '. A native tile and its Item belong to '
            'the link step: remove each Item with its tile, or restore the '
            'tile, then regenerate')


def regenerate_cell(catalog, directory: Path, quantity, state, origin,
                    validate: bool) -> Tuple[bool, int, int, int]:
    """
    Rewrite one cell: its overview Items, then its Collection.

    :returns: ``(collection_written, items, overview_items_written,
        overview_items_removed)``.
    """
    existing_files = catalog.read_item_files(directory)
    refuse_orphan_native_items(directory, existing_files)
    existing = [item for _, item in existing_files]
    overview = overview_items.build_overview_items(
        directory, quantity=quantity, state=state, origin=origin,
        existing_items=existing)
    live = {item['id'] for item in overview}
    removed = 0
    for path, item in existing_files:
        if overview_items.is_overview_item(item) and item.get('id') not in live:
            # Its tile was pruned: the product is gone, so is its record --
            # the file it was READ from, not one named from its id.
            path.unlink(missing_ok=True)
            removed += 1
    written = catalog.write_items(directory, overview, validate=validate)
    _, collection_written, items = catalog.regenerate_collection(
        directory, quantity=quantity, state=state, origin=origin,
        description=f'{quantity.value} / {state.value} / {origin.value}',
        validate=validate)
    return collection_written, len(items), len(written), removed


def _report(directory, result) -> None:
    written, items, overviews_written, overviews_removed = result
    print(f'{"wrote" if written else "unchanged"}: '
          f'{layout.collection_path(directory)} ({items} item(s); overview '
          f'Items: {overviews_written} written, {overviews_removed} removed)')


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Rebuild the Collection of one layer, or of every present cell."""
    args = build_parser().parse_args(argv)
    validate = not args.no_validate

    if args.layer_dir is not None:
        directory = Path(args.layer_dir).expanduser()
        if not directory.is_dir():
            raise OSError(f'not a directory: {directory}')
        quantity, state, origin = layer_cell(directory)
        print(f'layer: {directory}')
        result = regenerate_cell(
            stac_catalog(), directory, quantity, state, origin, validate)
        _report(directory, result)
        return 0

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
                result = regenerate_cell(
                    catalog, directory, quantity, state, origin, validate)
                changed += 1 if result[0] else 0
                _report(directory, result)
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
