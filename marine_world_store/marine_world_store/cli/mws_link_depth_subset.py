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

r"""
``mws_link_depth_subset`` -- the rev-3 tree over an existing depth subset.

An **adapter**, not a builder: see :mod:`marine_world_store.depth_subset` for
exactly what it claims and what it does not. In one line -- it copies existing
``marine_bathymetry_store`` tiles byte-identical under
``<root>/depths/<state>/<origin>/`` and writes the rev-3 record for them.

The bag paths are arguments or entries in a subset manifest, never literals:
they name read-only material on one particular host.

Typical use, with a subset manifest::

    mws_link_depth_subset --source-store <existing store>/depths \\
        --layer processed --store-root /tmp/rev3 --subset subset.yaml

where ``subset.yaml`` is::

    sources:
      - path: /mnt/.../logs/bizzyboat_sonar/2026-06-22T13-22-29+00-00
        platform: bizzyboat
    levels: [12]
    start: '2026-06-22T13:22:29Z'
    end: '2026-06-22T15:00:00Z'
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Optional, Sequence

from marine_world_store import depth_subset, item_schema, layout
from marine_world_store import source_identity
from marine_world_store.cli._common import (
    add_store_root_argument, resolved_root, run, stac_catalog,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_link_depth_subset', description=__doc__.splitlines()[0])
    parser.add_argument('--source-store', required=True, metavar='DIR',
                        help='existing bathymetry store (read-only)')
    parser.add_argument('--layer', default='processed',
                        help='layer directory inside it (default: processed)')
    add_store_root_argument(parser)
    parser.add_argument('--state', default=layout.State.REVIEWED.value,
                        choices=[s.value for s in layout.State])
    parser.add_argument('--origin', default=layout.Origin.SURVEYED.value,
                        choices=[o.value for o in layout.Origin])
    parser.add_argument('--source', action='append', default=[],
                        metavar='PATH',
                        help='a bag directory or file these tiles came from '
                             '(repeatable; or use --subset)')
    parser.add_argument('--subset', default=None, metavar='FILE',
                        help='YAML/JSON subset manifest naming the sources')
    parser.add_argument('--level', action='append', type=int, default=None,
                        help='adapt only this level (repeatable)')
    parser.add_argument('--builder-version', default='mws_link_depth_subset/1',
                        help='recorded in every fingerprint')
    parser.add_argument('--revision', action='append', default=[],
                        metavar='ID',
                        help='revision id that applies (repeatable)')
    parser.add_argument('--start', default=None, metavar='RFC3339')
    parser.add_argument('--end', default=None, metavar='RFC3339')
    parser.add_argument('--no-validate', action='store_true',
                        help='skip STAC validation of the written Items')
    parser.add_argument('--dry-run', action='store_true',
                        help='report what would be written, copy nothing')
    return parser


def _load_subset(path: Path) -> dict:
    text = path.read_text()
    if path.suffix.lower() == '.json':
        return json.loads(text)
    import yaml
    document = yaml.safe_load(text)
    if not isinstance(document, dict):
        raise ValueError(f'{path}: expected a mapping, got '
                         f'{type(document).__name__}')
    return document


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Adapt the subset and write its Items."""
    args = build_parser().parse_args(argv)
    document = {}
    if args.subset:
        document = _load_subset(Path(args.subset).expanduser())
    entries = [{'path': p} for p in args.source]
    if document:
        entries += depth_subset.sources_from_manifest(document)
    if not entries:
        raise ValueError(
            'name at least one --source bag, or a --subset manifest listing '
            'them: the tiles fingerprint over the sources they came from')

    levels = args.level or document.get('levels')
    start = args.start or document.get('start')
    end = args.end or document.get('end')

    root = resolved_root(args)
    source_ids = []
    source_items = []
    for entry in entries:
        path = Path(entry['path']).expanduser()
        if not path.exists():
            raise FileNotFoundError(f'{path}: no such file or directory')
        identifier = source_identity.source_id(path)
        source_ids.append(identifier)
        keys = (source_identity.merkle_lines(path) if path.is_dir()
                else [f'{path.name}\t{source_identity.file_key(path)}'])
        source_items.append(item_schema.build_source_item(
            source_id=identifier,
            kind='bag' if path.is_dir() else 'file',
            name=entry.get('name') or path.name,
            file_keys=keys,
            platform=entry.get('platform'),
            recorder=entry.get('recorder'),
            start_datetime=entry.get('start') or start,
            end_datetime=entry.get('end') or end,
            href=str(path),
        ))
        print(f'source {path.name}: {identifier}')

    report = depth_subset.adapt_depth_tiles(
        source_layer_dir=Path(args.source_store).expanduser() / args.layer,
        root=root.path,
        source_ids=source_ids,
        builder_version=args.builder_version,
        state=args.state,
        origin=args.origin,
        revision_ids=args.revision or None,
        levels=levels,
        start_datetime=start,
        end_datetime=end,
        dry_run=args.dry_run,
    )
    print(f'{report.tiles_seen} tile(s) in scope -> {report.destination}')
    if args.dry_run:
        print('dry run: nothing copied, no Item written')
        return 0
    print(f'copied {report.tiles_copied}, unchanged {report.tiles_unchanged}')

    changed = stac_catalog().write_items(
        layout.sources_dir(root.path), source_items,
        validate=not args.no_validate)
    print(f'sources/: {len(changed)} item(s) written, '
          f'{len(source_items) - len(changed)} unchanged')

    depth_subset.write_report_items(report, validate=not args.no_validate)
    print(f'depths/: {report.items_written} item(s) written, '
          f'{report.items_unchanged} unchanged')
    if report.missing_geometric_error:
        # Named, never silently zeroed: the D7 selection core falls back to
        # level-as-resolution for these, and that is a visible difference.
        print(f'note: {report.missing_geometric_error} tile(s) had no '
              'geometric_error_m in the source coverage manifest; their Items '
              'record none rather than a zero')
    return 0


def console_main() -> int:
    """Entry point (``console_scripts``)."""
    return run(main)


if __name__ == '__main__':
    raise SystemExit(console_main())
