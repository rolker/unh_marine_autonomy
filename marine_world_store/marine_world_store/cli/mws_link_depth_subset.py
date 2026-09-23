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

The bag directories are **required** arguments (``--source``, repeatable, or
a ``--subset`` manifest listing them), never literals: they name read-only
material on one particular host. They are required for two reasons, not one:
they are what the tiles fingerprint over, and they are what the tiles are
**dated** by. Each bag's ``metadata.yaml`` gives its recording interval, and
the tile Items carry the union of them. A source whose interval cannot be
read is refused and nothing is written -- ``--start``/``--end`` (or
``start:``/``end:`` at the top of the manifest) state an interval for the
sources that have one but do not record it, and are an operator statement
rather than a default: a bag that records its own interval keeps it. A
per-source ``start:``/``end:`` pair in a manifest entry is a statement about
that source alone, and wins over what it records.

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
from marine_world_store import source_identity, source_time
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
                             '(repeatable; or use --subset). Required: it is '
                             'what the tiles fingerprint over and what they '
                             'are dated by')
    parser.add_argument('--subset', default=None, metavar='FILE',
                        help='YAML/JSON subset manifest naming the sources')
    parser.add_argument('--level', action='append', type=int, default=None,
                        help='adapt only this level (repeatable)')
    parser.add_argument('--builder-version', default='mws_link_depth_subset/1',
                        help='recorded in every fingerprint')
    parser.add_argument('--revision', action='append', default=[],
                        metavar='ID',
                        help='revision id that applies (repeatable)')
    parser.add_argument(
        '--start', default=None, metavar='RFC3339',
        help="state the sources' interval instead of reading it from their "
             'metadata.yaml; both ends or neither')
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


def _source_interval(entry, path, start, end):
    """
    Date one source: its own statement, else its record, else the fallback.

    Precedence, and why:

    1. an entry's own ``start:``/``end:`` -- an operator statement about THIS
       source, taken as a pair (both or neither; half of one is refused rather
       than completed from the global pair, which would stitch one source's
       start to another statement's end);
    2. the interval the source RECORDED (a bag's ``metadata.yaml``). A recorded
       interval is evidence; a manifest-wide statement is not about this bag;
    3. the global ``--start/--end`` (or manifest ``start:``/``end:``) -- only
       for a source that records none, which is what the flag is documented
       for.

    Regression: the global pair used to override every source, bags that
    recorded their own interval included, and could be mixed with an entry's
    half-stated one.
    """
    entry_start, entry_end = entry.get('start'), entry.get('end')
    if entry_start or entry_end:
        return source_time.check_interval(
            entry_start, entry_end, what=f'source {path.name}')
    try:
        return source_time.source_interval(path)
    except source_time.TimeIntervalError:
        if not (start or end):
            raise
    print(f'source {path.name}: records no interval; using the stated '
          f'--start/--end')
    return source_time.check_interval(
        start, end, what=f'the stated interval for source {path.name}')


def resolve_sources(entries, start=None, end=None):
    """
    Identify and date every source entry; return ids, Items and intervals.

    ``start``/``end`` are the global stated interval: the fallback for a
    source that records none, never an override (see :func:`_source_interval`).

    A source named twice -- on the command line and in the manifest, or twice
    in one manifest -- is ONE source: its content id is what identifies it, so
    a repeat is dropped (and said so) rather than fingerprinted twice and
    written twice.
    """
    source_ids = []
    source_items = []
    intervals = []
    seen = {}
    for entry in entries:
        path = Path(entry['path']).expanduser()
        if not path.exists():
            raise FileNotFoundError(f'{path}: no such file or directory')
        identifier = source_identity.source_id(path)
        if identifier in seen:
            print(f'source {path}: same content as {seen[identifier]} '
                  f'({identifier}); counted once')
            continue
        seen[identifier] = path
        entry_start, entry_end = _source_interval(entry, path, start, end)
        intervals.append((entry_start, entry_end))
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
            start_datetime=entry_start,
            end_datetime=entry_end,
            href=str(path),
        ))
        print(f'source {path.name}: {identifier} '
              f'({entry_start} .. {entry_end})')
    return source_ids, source_items, intervals


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
            'them: the tiles fingerprint over the sources they came from, '
            'and are dated from their recorded intervals')

    levels = args.level or document.get('levels')
    start = args.start or document.get('start')
    end = args.end or document.get('end')

    root = resolved_root(args)
    source_ids, source_items, intervals = resolve_sources(entries, start, end)

    # A product is dated by everything that went into it: a tile built from
    # three bags was observed over all three.
    tile_start, tile_end = source_time.union_intervals(
        intervals, what='the adapted tiles')
    print(f'tiles observed: {tile_start} .. {tile_end}')

    report = depth_subset.adapt_depth_tiles(
        source_layer_dir=Path(args.source_store).expanduser() / args.layer,
        root=root.path,
        source_ids=source_ids,
        builder_version=args.builder_version,
        state=args.state,
        origin=args.origin,
        revision_ids=args.revision or None,
        levels=levels,
        start_datetime=tile_start,
        end_datetime=tile_end,
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
