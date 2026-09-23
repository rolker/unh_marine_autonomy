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
Keep an unchanged tile from *looking* changed to a mtime-driven DAG.

Design section 9 makes **fingerprints, not mtimes**, the trigger for a
regenerate. Snakemake's own DAG is mtime-based, so the two only agree if
something reconciles them: a tile that a rebuild rewrote byte for byte is newer
by mtime and identical by content, and every rule above it would re-run for
nothing.

This is that reconciliation, the prototype's component 5 pre-step. Each tile
gets a ``.fp`` sidecar holding the tile's **content** fingerprint. On a refresh:

* the content matches the sidecar -- the tile's mtime is reset to the sidecar's,
  so the DAG sees no change, because there is none;
* the content differs -- the sidecar is rewritten and the tile's mtime is left
  alone, so the DAG re-runs what depends on it.

**This is not section 9's fingerprint.** That one is over a product's *inputs*
(sources, revisions, decoder and builder versions) and lives in the tile's Item;
it answers "was this built from the same things?". The one here is over the
tile's *bytes* and answers only "did this file actually change?". They are
different questions and neither substitutes for the other -- which is why the
sidecar records which it is, rather than being a bare hash a later reader could
mistake for the other one.

The mtime reset is the only write to the tile itself, and it never touches its
content.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
from typing import Dict, List, Union

from marine_world_store import atomic_io, layout

PathLike = Union[str, Path]

#: What a ``.fp`` document is, so a reader cannot mistake it for section 9's
#: input fingerprint.
SCHEMA = 'tile-content-fingerprint/1'

#: Suffix of the sidecar beside a tile.
SUFFIX = '.fp'

_CHUNK = 1 << 20


def sidecar_path(tile: PathLike) -> Path:
    """``<tile>.fp`` -- beside the tile, not in a parallel tree."""
    tile = Path(tile)
    return tile.with_name(tile.name + SUFFIX)


def content_fingerprint(tile: PathLike) -> str:
    """Return the sha256 of a tile's bytes, read in chunks."""
    digest = hashlib.sha256()
    with open(tile, 'rb') as handle:
        for chunk in iter(lambda: handle.read(_CHUNK), b''):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass
class RefreshReport:
    """What one refresh pass did."""

    #: Tiles whose content matched their sidecar; their mtime was reset.
    unchanged: int = 0
    #: Tiles whose content differed; their sidecar was rewritten.
    changed: int = 0
    #: Tiles that had no sidecar; one was written. Their mtime is left alone --
    #: a first run has nothing to compare against and must not claim the tile
    #: is older than it is.
    created: int = 0
    #: Sidecars whose tile is gone. Removed: a stale ``.fp`` would keep
    #: asserting a fingerprint for a file nothing can check it against.
    orphans_removed: int = 0
    #: Sidecars that could not be read and were treated as absent.
    unreadable: List[str] = None    # type: ignore[assignment]
    #: Tiles that are symbolic links, left entirely alone. ``os.utime``
    #: follows a link, so "resetting the tile's mtime" would reach through it
    #: and rewrite the mtime of whatever it points at -- possibly a file in a
    #: store this pass has no business touching. A rev-3 layer holds real
    #: copies (the adapter copies byte-identical); a link is named, not
    #: reconciled.
    symlinks_skipped: List[str] = None    # type: ignore[assignment]

    def __post_init__(self) -> None:
        """Give the list fields per-instance lists."""
        if self.unreadable is None:
            self.unreadable = []
        if self.symlinks_skipped is None:
            self.symlinks_skipped = []


def _read_sidecar(path: Path) -> Dict[str, object]:
    try:
        document = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    if not isinstance(document, dict) or document.get('schema') != SCHEMA:
        # A document of some other schema is not this one's to interpret --
        # treat it as absent and rewrite, rather than guessing its fields.
        return {}
    return document


def _write_sidecar(path: Path, fingerprint: str) -> None:
    # Atomic: a reader sees the old document or the new one, never a half
    # one, and a crashed run leaves no sidecar claiming a truncated hash.
    atomic_io.write_text(path, json.dumps(
        {'schema': SCHEMA, 'fingerprint': fingerprint}, indent=2) + '\n')


def refresh_tile(tile: PathLike, report: RefreshReport) -> str:
    """
    Reconcile one tile with its sidecar. Returns the outcome's name.

    ``unchanged`` (mtime reset), ``changed`` or ``created``.
    """
    tile = Path(tile)
    sidecar = sidecar_path(tile)
    fingerprint = content_fingerprint(tile)
    document = _read_sidecar(sidecar)
    if sidecar.exists() and not document:
        report.unreadable.append(str(sidecar))
    if document.get('fingerprint') == fingerprint:
        # The content is identical, so the tile is not newer than the record of
        # it however many times a rebuild rewrote the file.
        sidecar_mtime = sidecar.stat().st_mtime
        os.utime(tile, (sidecar_mtime, sidecar_mtime))
        report.unchanged += 1
        return 'unchanged'
    existed = bool(document)
    _write_sidecar(sidecar, fingerprint)
    if existed:
        report.changed += 1
        return 'changed'
    report.created += 1
    return 'created'


def refresh_directory(
    directory: PathLike, remove_orphans: bool = True,
) -> RefreshReport:
    """Refresh every ``*.tif`` in ``directory`` (not recursive)."""
    directory = Path(directory)
    report = RefreshReport()
    if not directory.is_dir():
        raise OSError(f'not a directory: {directory}')
    tiles = sorted(layout.tiles_in_dir(directory))
    for tile in tiles:
        if tile.is_symlink():
            report.symlinks_skipped.append(str(tile))
            continue
        refresh_tile(tile, report)
    if remove_orphans:
        live = {sidecar_path(tile) for tile in tiles if not tile.is_symlink()}
        for sidecar in sorted(directory.glob('*' + SUFFIX)):
            if sidecar not in live:
                sidecar.unlink()
                report.orphans_removed += 1
    return report


def refresh_layer(layer_dir: PathLike) -> Dict[str, RefreshReport]:
    """
    Refresh a quantity layer's native tiles and its ``overviews/`` sidecar.

    Reported separately, because the two are different kinds of thing: the
    native tiles are the compile this store does not own, and the overviews are
    the derived product the DAG rebuilds.

    :raises marine_world_store.layout.LayoutError: on a legacy
        ``draft/processed/reference/chart`` layer -- this pass writes and
        deletes ``.fp`` files and moves tile mtimes, none of which it may do
        to the legacy tree (:func:`marine_world_store.layout.refuse_legacy_layer`).
    """
    layer_dir = layout.refuse_legacy_layer(layer_dir)
    reports = {'native': refresh_directory(layer_dir)}
    overviews = layout.overviews_dir(layer_dir)
    if overviews.is_dir():
        reports['overviews'] = refresh_directory(overviews)
    return reports
