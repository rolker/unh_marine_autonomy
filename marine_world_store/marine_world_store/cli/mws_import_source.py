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
``mws_import_source`` -- identify a source and record it in ``sources/``.

Computes the content id (design section 3: a Merkle id over a bag directory's
data files, or a single file's git-annex key) and writes the ``sources/`` Item
that carries it, together with the lookup metadata -- platform, recorder, time
range -- that is deliberately *not* part of the identity.

The **time range is read from the bag** (``metadata.yaml``: the recording's
start plus its duration), never asked for and never guessed. A source that
carries no readable interval is refused: ``--start``/``--end`` exist for the
sources that have one but do not record it -- a cast file, a prior grid --
and are an operator statement, not a default.

The material itself is never copied, moved or modified: a source is immutable
and stays where it is. Annexing it (prototype component 10) is future work.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Optional, Sequence

from marine_world_store import item_schema, layout, source_identity
from marine_world_store import source_time
from marine_world_store.cli._common import (
    add_store_root_argument, resolved_root, run, stac_catalog,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_import_source', description=__doc__.splitlines()[0])
    parser.add_argument(
        'path', help='the bag directory or single file to identify')
    add_store_root_argument(parser)
    parser.add_argument('--kind', default=None,
                        help='what the source is (default: bag or file)')
    parser.add_argument('--name', default=None,
                        help='human name (default: the path basename)')
    parser.add_argument('--role', default='data',
                        choices=('data', 'engineering'),
                        help='engineering material is indexed, never an input')
    parser.add_argument('--platform', default=None,
                        help='lookup metadata, never identity')
    parser.add_argument('--recorder', default=None)
    parser.add_argument(
        '--start', default=None, metavar='RFC3339',
        help='state the interval instead of reading it from the bag; both '
             'ends or neither')
    parser.add_argument('--end', default=None, metavar='RFC3339')
    parser.add_argument('--notes', default=None)
    parser.add_argument(
        '--dry-run', action='store_true',
        help='compute and print the id, write nothing')
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Identify the source and write its Item."""
    args = build_parser().parse_args(argv)
    path = Path(args.path).expanduser()
    if not path.exists():
        raise FileNotFoundError(f'{path}: no such file or directory')

    is_bag = path.is_dir()
    if args.start or args.end:
        start, end = source_time.check_interval(
            args.start, args.end, what=f'source {path.name}')
    else:
        start, end = source_time.source_interval(path)
    identifier = source_identity.source_id(path)
    keys = source_identity.merkle_lines(path) if is_bag else [
        f'{path.name}\t{source_identity.file_key(path)}']
    print(f'source id: {identifier}')
    print(f'observed: {start} .. {end}')
    if args.dry_run:
        return 0

    root = resolved_root(args)
    item = item_schema.build_source_item(
        source_id=identifier,
        kind=args.kind or ('bag' if is_bag else 'file'),
        name=args.name or path.name,
        file_keys=keys,
        role=args.role,
        platform=args.platform,
        recorder=args.recorder,
        start_datetime=start,
        end_datetime=end,
        href=str(path),
        notes=args.notes,
    )
    written_path, changed = stac_catalog().write_item(
        layout.sources_dir(root.path), item)
    print(f'{"wrote" if changed else "unchanged"}: {written_path}')
    return 0


def console_main() -> int:
    """Entry point (``console_scripts``)."""
    return run(main)


if __name__ == '__main__':
    raise SystemExit(console_main())
