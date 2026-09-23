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
Assemble the per-tile overview records into one coverage manifest.

The per-parent writer (``build_depth_overview_parent``) leaves a
``<level>_<row>_<col>.json`` record beside each tile it writes, carrying that
tile's ``geometric_error_m``, band schema and sigma rule. It deliberately does
**not** touch a shared ``coverage.json``: a parallel DAG rebuilding many
parents at once would be a write race over one file, and the only lock that
would fix it is one that serialises the DAG back into the batch build the
per-parent mode exists to replace.

So the manifest is assembled once, after the DAG, from records that were each
written by exactly one process. This module is that assembly, and it writes the
same ``coverage-manifest/1`` document the C++ batch writer does -- the schema
authority is ``marine_tiled_raster_store/.../coverage_manifest.hpp``, and
:mod:`marine_world_store.coverage` is the reader that has to agree with it.

Why it matters: uma-ADR-0013 D2 makes nested per-tile geometric error a
**producer** obligation, and D3 puts it in this manifest. A rev-3 overview tile
with no manifest entry falls back to level-as-resolution, which is exactly the
approximation the D7 selection core (uma#395) exists to stop making.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from marine_world_store import atomic_io, coverage, layout

PathLike = Union[str, Path]

#: The record the C++ per-parent writer leaves beside each tile.
TILE_RECORD_SCHEMA = 'depth-overview-tile/1'

#: The manifest schema, as the C++ writer and reader spell it.
MANIFEST_SCHEMA = 'coverage-manifest/1'

TileKey = Tuple[int, int, int]


@dataclass
class TileRecord:
    """One derived tile's own record."""

    key: TileKey
    geometric_error_m: Optional[float]
    sigma_fold: Optional[str]
    sigma_band_written: bool = False
    #: The contributors the tile folded, relative to the layer (``<name>``
    #: native, ``overviews/<name>`` derived); ``None`` for a record that does
    #: not name them. The lineage an overview tile's Item is built from.
    children: Optional[List[str]] = None


def read_tile_records(overviews_dir: PathLike) -> Dict[TileKey, TileRecord]:
    """
    Read every per-tile record in ``overviews_dir``.

    A record whose tile is gone is ignored: it describes nothing, and carrying
    it into the manifest would advertise coverage that is not there.

    :raises ValueError: a record of this schema whose ``geometric_error_m`` is
        neither null nor a finite, non-negative number.
    """
    overviews_dir = Path(overviews_dir)
    records: Dict[TileKey, TileRecord] = {}
    if not overviews_dir.is_dir():
        return records
    for path in sorted(overviews_dir.glob('*.json')):
        key = layout.parse_tile_filename(path.with_suffix('.tif').name)
        if key is None:
            continue
        if not path.with_suffix('.tif').is_file():
            continue
        try:
            document = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        if (not isinstance(document, dict) or
                document.get('schema') != TILE_RECORD_SCHEMA):
            continue
        raw = document.get('geometric_error_m')
        # An unrecorded error is None, never 0: uma-ADR-0013 D1's reader
        # contract is that absence means "fall back", not "no error". Anything
        # else must be a finite, non-negative length -- the rule the manifest
        # READER applies (coverage.parse_geometric_error). This is the
        # manifest's writer, so an invalid value is refused here rather than
        # published: a bare NaN is not JSON, and the C++ reader rejects the
        # whole document over one.
        error = coverage.parse_geometric_error(raw)
        if error is coverage.INVALID or (error is None and raw is not None):
            raise ValueError(
                f'{path}: geometric_error_m {raw!r} is not a finite, '
                'non-negative length (or null for unrecorded); refusing to '
                'publish it -- rebuild the tile')
        children = document.get('children')
        if not (isinstance(children, list) and
                all(isinstance(c, str) and c for c in children)):
            children = None
        records[key] = TileRecord(
            key=key,
            geometric_error_m=error,
            sigma_fold=document.get('sigma_fold'),
            sigma_band_written=bool(document.get('sigma_band_written', False)),
            children=children,
        )
    return records


def encode_manifest(
    records: Dict[TileKey, TileRecord], kind: str = 'derived',
) -> Dict[str, object]:
    """
    Run-encode the records as a ``coverage-manifest/1`` document.

    Contiguous column runs within one row, merged only where the neighbours
    **agree on the geometric error**, so the error stays per tile rather than
    being widened to a per-row maximum. That is the C++ encoder's rule, and a
    reader cannot tell the two documents apart.
    """
    by_level: Dict[int, List[TileKey]] = {}
    for key in sorted(records):
        by_level.setdefault(key[0], []).append(key)
    levels: List[Dict[str, object]] = []
    for level in sorted(by_level):
        runs: List[Dict[str, object]] = []
        for (_level, row, col) in by_level[level]:
            error = records[(level, row, col)].geometric_error_m
            if (runs and runs[-1]['row'] == row and
                    runs[-1]['col_max'] + 1 == col and
                    runs[-1]['geometric_error_m'] == error):
                runs[-1]['col_max'] = col
                continue
            runs.append({'row': row, 'col_min': col, 'col_max': col,
                         'geometric_error_m': error})
        levels.append({'level': level, 'runs': runs})
    return {'schema': MANIFEST_SCHEMA, 'kind': kind, 'levels': levels}


def manifest_records(overviews_dir: PathLike
                     ) -> Tuple[Dict[TileKey, TileRecord], List[TileKey]]:
    """
    One entry per TILE on disk, recorded or not; and which had no record.

    The manifest describes the tiles, not the records. A tile with no
    per-tile record -- written by the batch builder, which records errors in
    ``coverage.json`` only, or left by a crash between the tile's rename and
    its record's -- is still coverage. Dropping it (as the first version did)
    rewrote ``coverage.json`` with FEWER tiles than the directory holds, and
    for a batch-built pyramid threw away every error the batch builder had
    recorded. Such a tile keeps the error the existing manifest already gives
    it, and otherwise ``None`` (fall back; never 0).

    :returns: ``(records, unrecorded)`` -- ``unrecorded`` in GGGS order, so a
        caller can name them.
    """
    overviews_dir = Path(overviews_dir)
    records = read_tile_records(overviews_dir)
    existing = coverage.load_coverage_manifest(
        layout.coverage_manifest_path(overviews_dir))
    unrecorded: List[TileKey] = []
    for tile in layout.tiles_in_dir(overviews_dir):
        key = layout.parse_tile_filename(tile.name)
        if key is None or key in records:
            continue
        unrecorded.append(key)
        records[key] = TileRecord(
            key=key,
            geometric_error_m=(
                existing.geometric_error(key) if existing else None),
            sigma_fold=None)
    return records, sorted(unrecorded)


def assemble(overviews_dir: PathLike, kind: str = 'derived') -> Path:
    """
    Write ``<overviews_dir>/coverage.json`` over every tile in the directory.

    Built from the per-tile records, with each unrecorded tile carried over as
    :func:`manifest_records` describes. Atomic publish (private temporary,
    fsync, rename), the same guarantee ``saveCoverageManifest`` gives: a reader
    sees the whole previous document or the whole new one.

    An unchanged manifest is left alone, bytes and mtime (design section 9's
    replica rule): the regenerate DAG reassembles on every run, and a
    rewrite that changed nothing would still make a replica sync move it.
    """
    overviews_dir = Path(overviews_dir)
    if not overviews_dir.is_dir():
        raise OSError(f'not a directory: {overviews_dir}')
    records, _ = manifest_records(overviews_dir)
    document = encode_manifest(records, kind=kind)
    path = layout.coverage_manifest_path(overviews_dir)
    text = json.dumps(document, indent=2) + '\n'
    try:
        unchanged = path.read_text() == text
    except OSError:
        unchanged = False
    if not unchanged:
        atomic_io.write_text(path, text)
    return path


def sigma_folds(overviews_dir: PathLike) -> Dict[str, int]:
    """
    Count the sigma rules the tiles were written under.

    A layer whose tiles disagree is a layer built across a rule change, which
    is a new fingerprint and not a migration -- the counts make that visible
    instead of leaving one number to be read as the whole layer's.
    """
    counts: Dict[str, int] = {}
    for record in read_tile_records(overviews_dir).values():
        name = record.sigma_fold or 'unrecorded'
        counts[name] = counts.get(name, 0) + 1
    return counts
