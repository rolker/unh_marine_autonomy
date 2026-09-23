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
Decide from a tile's CONTENT whether it changed, and tell a mtime-driven DAG.

Design section 9 makes **fingerprints, not mtimes**, the trigger for a
regenerate. Snakemake's own DAG is mtime-based, so the two only agree if
something reconciles them -- and a tile's mtime as found cannot be trusted in
EITHER direction: a rebuild that rewrote a tile byte for byte made it newer
without changing it, and a copy that preserves mtimes (``shutil.copy2``,
``rsync -t``, a restore, re-linking an older compile) changed it while leaving
it OLDER than the products built from it.

This is that reconciliation, the prototype's component 5 pre-step. Each tile
gets a ``.fp`` sidecar holding the tile's **content** fingerprint and the mtime
this module gave the tile when that content was recorded. On a refresh the
decision is made from the content alone, and the mtime is then SET to say it:

* the content matches the sidecar -- the tile's mtime is reset to the recorded
  one, so the DAG sees no change, because there is none;
* the content differs, or there is no sidecar -- the tile's mtime is advanced
  to now, and that is what is recorded, so the DAG
  sees a change newer than anything built from the old content.

A tile the DAG itself has just built is recorded as built
(:func:`record_tile`), with the mtime the build gave it: it is newer than the
children it was folded from, and it keeps that order even when the rebuild
came out byte for byte the same as before. (Resetting it to its FIRST-seen
mtime instead left it older than a child whose change it absorbed, and the DAG
rebuilt it and everything above it on every later run.)

A DERIVED tile (``overviews/``) is held to a stricter rule, because advancing
it would be a lie: a derived tile is newer than its children only because it
was folded FROM them. One whose content does not match its sidecar -- restored
from a backup, half-copied, rotted -- or that has no sidecar at all was not
built by this DAG from what is there now, and moving its mtime to now would
make it look newer than its children, so it would never be rebuilt while its
parents were rebuilt from it. It is **removed**, with its per-tile record and
its sidecar, and the DAG rebuilds it as a missing product.

The first refresh over a layer with no sidecars therefore marks every native
tile as changed and removes every derived tile: with nothing recorded, there is
nothing to prove any product was built from what is there now, so the whole
pyramid is rebuilt once.

**This is not section 9's fingerprint.** That one is over a product's *inputs*
(sources, revisions, decoder and builder versions) and lives in the tile's Item;
it answers "was this built from the same things?". The one here is over the
tile's *bytes* and answers only "did this file actually change?". They are
different questions and neither substitutes for the other -- which is why the
sidecar records which it is, rather than being a bare hash a later reader could
mistake for the other one.

For a native tile the mtime is the only write, and it never touches its
content; the store does not own the compile. A derived tile is the store's own
regenerable product, and removing an untrustworthy one is the only way to have
it rebuilt.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
from typing import Dict, List, Optional, Union

from marine_world_store import atomic_io, layout

PathLike = Union[str, Path]

#: What a ``.fp`` document is, so a reader cannot mistake it for section 9's
#: input fingerprint. ``mtime_ns`` is the mtime this module gave the tile when
#: the fingerprint was recorded.
SCHEMA = 'tile-content-fingerprint/2'

#: The superseded schema: no recorded mtime, so it is re-recorded (as for a
#: tile seen for the first time) rather than reported as unreadable.
_SUPERSEDED_SCHEMAS = frozenset({'tile-content-fingerprint/1'})

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
    #: Tiles whose content differed; mtime advanced, sidecar rewritten.
    changed: int = 0
    #: Tiles that had no sidecar; mtime advanced, one written. Nothing proves
    #: a product was built from this content, so it counts as a change.
    created: int = 0
    #: Sidecars whose tile is gone. Removed: a stale ``.fp`` would keep
    #: asserting a fingerprint for a file nothing can check it against.
    orphans_removed: int = 0
    #: Sidecars that could not be read and were treated as absent.
    unreadable: List[str] = None    # type: ignore[assignment]
    #: DERIVED tiles removed (with their record and sidecar) because their
    #: content did not match their sidecar, or they had none: nothing shows
    #: they were built from what is there now, so the DAG rebuilds them.
    removed: List[str] = None    # type: ignore[assignment]
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
        if self.removed is None:
            self.removed = []
        if self.symlinks_skipped is None:
            self.symlinks_skipped = []


def _read_sidecar(path: Path) -> Optional[Dict[str, object]]:
    """
    Return the sidecar's document; ``{}`` when absent; ``None`` if unreadable.

    A superseded-schema document reads as absent (it is re-recorded), a
    document of any other schema or shape as unreadable -- it is not this
    module's to interpret, so it is rewritten rather than guessed at.
    """
    if not path.exists():
        return {}
    try:
        document = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    if isinstance(document, dict) and \
            document.get('schema') in _SUPERSEDED_SCHEMAS:
        return {}
    if not isinstance(document, dict) or document.get('schema') != SCHEMA \
            or not isinstance(document.get('mtime_ns'), int):
        return None
    return document


def _write_sidecar(path: Path, fingerprint: str, mtime_ns: int) -> None:
    # Atomic: a reader sees the old document or the new one, never a half
    # one, and a crashed run leaves no sidecar claiming a truncated hash.
    atomic_io.write_text(path, json.dumps(
        {'schema': SCHEMA, 'fingerprint': fingerprint, 'mtime_ns': mtime_ns},
        indent=2) + '\n')


def _set_mtime(tile: Path, stat: os.stat_result, mtime_ns: int) -> None:
    """
    Set a tile's mtime to a recorded value, naming an ownership refusal.

    Setting explicit times needs OWNERSHIP of the file, not just write
    access; a tile written by another uid (a container run as someone else)
    otherwise stopped the pre-step with a bare PermissionError.
    """
    try:
        os.utime(tile, ns=(stat.st_atime_ns, mtime_ns))
    except PermissionError as exc:
        raise PermissionError(
            f'{tile}: owned by uid {stat.st_uid}, not this user (uid '
            f'{os.getuid()}); the fingerprint pre-step sets tile mtimes, which '
            'needs ownership -- run the regenerate as the layer\'s owner, or '
            'give the layer a single owner') from exc


def _remove_derived(tile: Path) -> None:
    """Remove a derived tile with its per-tile record and its sidecar."""
    for path in (tile, tile.with_suffix('.json'), sidecar_path(tile)):
        path.unlink(missing_ok=True)


def refresh_tile(tile: PathLike, report: RefreshReport,
                 derived: bool = False) -> str:
    """
    Reconcile one tile with its sidecar. Returns the outcome's name.

    ``unchanged`` (mtime reset to the recorded one), ``changed`` or
    ``created`` (mtime advanced to now, and recorded) -- or, for a
    ``derived`` tile whose content is not the recorded content, ``removed``
    (see the module docstring).
    """
    tile = Path(tile)
    sidecar = sidecar_path(tile)
    fingerprint = content_fingerprint(tile)
    document = _read_sidecar(sidecar)
    if document is None:
        report.unreadable.append(str(sidecar))
        document = {}
    stat = tile.stat()
    if document.get('fingerprint') == fingerprint:
        # The content is identical, so the tile is as old as the content is,
        # however many times a rebuild rewrote the file.
        _set_mtime(tile, stat, document['mtime_ns'])
        report.unchanged += 1
        return 'unchanged'
    if derived:
        # Not built by this DAG from what is there now: advancing it would
        # make it look newer than its children and it would never be rebuilt.
        _remove_derived(tile)
        report.removed.append(str(tile))
        return 'removed'
    # Different content (or none recorded): whatever mtime the file arrived
    # with -- a copy2 of an older compile keeps its old one, a clock-skewed
    # sync a future one -- it must read as newer than every product built
    # before now, and older than every product built from it after.
    # "Now" by the FILESYSTEM's clock, as a build output gets: on NFS/SMB the
    # server's clock stamps what a rule writes, and a host clock behind it
    # would record the tile at or below the mtime of a parent built after it.
    os.utime(tile)
    mtime_ns = tile.stat().st_mtime_ns
    _write_sidecar(sidecar, fingerprint, mtime_ns)
    if document:
        report.changed += 1
        return 'changed'
    report.created += 1
    return 'created'


def record_tile(tile: PathLike) -> None:
    """
    Record a tile the DAG has just built, with the mtime the build gave it.

    Not a comparison: the build is the change, and its mtime is already newer
    than the inputs it was folded from. Recording it keeps that order on every
    later refresh, even when the rebuild came out byte for byte the same.
    A symbolic link is refused -- a build writes a real file.
    """
    tile = Path(tile)
    if tile.is_symlink():
        raise OSError(f'{tile}: a symbolic link, not a tile this DAG built')
    _write_sidecar(sidecar_path(tile), content_fingerprint(tile),
                   tile.stat().st_mtime_ns)


def refresh_directory(
    directory: PathLike, remove_orphans: bool = True, derived: bool = False,
) -> RefreshReport:
    """
    Refresh every ``*.tif`` in ``directory`` (not recursive).

    :param derived: the directory holds DERIVED tiles (``overviews/``): one
        whose content is not the recorded content is removed, not advanced.
    """
    directory = Path(directory)
    report = RefreshReport()
    if not directory.is_dir():
        raise OSError(f'not a directory: {directory}')
    tiles = sorted(layout.tiles_in_dir(directory))
    for tile in tiles:
        if tile.is_symlink():
            report.symlinks_skipped.append(str(tile))
            continue
        refresh_tile(tile, report, derived=derived)
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
    """
    layer_dir = Path(layer_dir)
    reports = {}
    overviews = layout.overviews_dir(layer_dir)
    if overviews.is_dir():
        reports['overviews'] = refresh_directory(overviews, derived=True)
    reports['native'] = refresh_directory(layer_dir)
    return reports
