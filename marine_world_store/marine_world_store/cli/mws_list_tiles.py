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
``mws_list_tiles`` -- the tiles a layer's Items record, one path per line.

What the derived tile index is built FROM. Design section 7: the Items and the
Collection are the record, and an index is derived from them -- so the file
list comes from the Items' data assets, not from globbing the directory (Part 2
line 1: enumeration, never globbing). A tile on disk that no Item records is
not in the index; an Item whose asset is missing is an error, because the
index would point at nothing.

``--kind`` selects the native tiles (2-band) or the derived overview tiles
(4-band): one tile index holds one band schema, so the two are indexed apart.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import List, Optional, Sequence

from marine_world_store import layout, overview_items
from marine_world_store.cli._common import run, stac_catalog


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_list_tiles', description=__doc__.splitlines()[1])
    parser.add_argument('layer_dir', metavar='LAYER_DIR',
                        help='a quantity layer (<root>/<q>/<state>/<origin>/)')
    parser.add_argument('--kind', choices=('native', 'overview'),
                        required=True,
                        help='native tiles, or the derived overview tiles')
    parser.add_argument(
        '--optfile', action='store_true',
        help=("quote each path, one per line, for a GDAL utility's "
              '--optfile (which splits unquoted lines on spaces)'))
    return parser


def tile_paths(layer_dir: Path, kind: str) -> List[Path]:
    """
    Resolve the data asset of every Item of ``kind`` in ``layer_dir``.

    :raises OSError: when an Item's asset is not on disk.
    """
    wanted_overview = kind == 'overview'
    paths = []
    for item in stac_catalog().read_items(layer_dir):
        if overview_items.is_overview_item(item) != wanted_overview:
            continue
        href = ((item.get('assets') or {}).get('data') or {}).get('href')
        if not href:
            raise OSError(f'Item {item.get("id")!r} has no data asset')
        path = (layer_dir / href).resolve()
        if not path.is_file():
            raise OSError(
                f'Item {item.get("id")!r} records {href}, which is not on '
                'disk; regenerate the catalog before the index')
        paths.append(path)
    return sorted(paths)


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Print the paths."""
    args = build_parser().parse_args(argv)
    layer_dir = Path(args.layer_dir).expanduser()
    if not layer_dir.is_dir():
        raise OSError(f'not a directory: {layer_dir}')
    layout.refuse_legacy_layer(layer_dir)
    for path in tile_paths(layer_dir, args.kind):
        print(f'"{path}"' if args.optfile else path)
    return 0


def console_main() -> int:
    """Entry point (``console_scripts``)."""
    return run(main)


if __name__ == '__main__':
    raise SystemExit(console_main())
