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
Content identity for a source (design draft section 3, spine decision 1).

A source's id is what it *is*, not what it says it is: the sha256 of the sorted
``<split filename>\\t<file key>`` lines of a bag directory's data files, where
the file key is git-annex's ``SHA256E`` key. Consequences that shape the code:

* ``metadata.yaml`` is excluded, so ``ros2 bag reindex`` -- which rewrites only
  the yaml and leaves every ``.mcap`` byte-identical -- is a legitimate repair
  rather than a new source.
* Every other non-data file is excluded too ("sensor data files only").
* Filenames are included: split order is meaningful and rosbag2 names splits
  deterministically.
* A single-file source (a cast, a prior grid) is its file key alone.

No git-annex dependency: the key is computed from the bytes on disk, in
git-annex's own format, so a later ``git annex add`` produces the same key for
the same file.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Iterable, Sequence, Tuple, Union

PathLike = Union[str, Path]

#: Suffixes counted as sensor data inside a bag directory. rosbag2's mcap
#: format is jazzy's default and is what the season recorded.
DATA_SUFFIXES: Tuple[str, ...] = ('.mcap',)

#: Never part of a bag's identity (see the module docstring).
EXCLUDED_NAMES = frozenset({'metadata.yaml'})

#: Read in blocks rather than whole: a season bag is 14 GB.
_BLOCK = 1024 * 1024


class SourceIdentityError(ValueError):
    """A source whose identity cannot be computed as specified."""


def file_key(path: PathLike) -> str:
    """
    git-annex ``SHA256E`` key for ``path``.

    Format: ``SHA256E-s<size>--<sha256hex><ext>``. git-annex keeps the
    extension in an ``E`` (extension-preserving) key, so the key of
    ``rosbag2_0.mcap`` ends in ``.mcap``; reproducing that here is what makes
    an id computed before annexing equal to the one computed after.
    """
    path = Path(path)
    if not path.is_file():
        raise SourceIdentityError(f'{path}: not a file')
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as handle:
        while True:
            block = handle.read(_BLOCK)
            if not block:
                break
            size += len(block)
            digest.update(block)
    return f'SHA256E-s{size}--{digest.hexdigest()}{path.suffix}'


def data_files(
    bag_dir: PathLike,
    suffixes: Sequence[str] = DATA_SUFFIXES,
) -> list:
    """
    List the identity-bearing files of ``bag_dir``, sorted by filename.

    :raises SourceIdentityError: when the directory holds no data file at all
        -- hashing an empty list would give every empty directory the same
        confident-looking id.
    """
    bag_dir = Path(bag_dir)
    if not bag_dir.is_dir():
        raise SourceIdentityError(f'{bag_dir}: not a directory')
    wanted = tuple(suffixes)
    found = [
        path for path in bag_dir.iterdir()
        if path.is_file()
        and path.name not in EXCLUDED_NAMES
        and path.suffix in wanted
    ]
    if not found:
        raise SourceIdentityError(
            f'{bag_dir}: no {"/".join(wanted)} file to identify it by')
    return sorted(found, key=lambda path: path.name)


def merkle_lines(
    bag_dir: PathLike,
    suffixes: Sequence[str] = DATA_SUFFIXES,
) -> list:
    r"""
    List the ``<filename>\\t<file key>`` lines a bag id is taken over.

    Exposed because a source Item records them: the id is then checkable
    against the files without recomputing every key.
    """
    return [f'{path.name}\t{file_key(path)}'
            for path in data_files(bag_dir, suffixes)]


def bag_source_id(
    bag_dir: PathLike,
    suffixes: Sequence[str] = DATA_SUFFIXES,
) -> str:
    r"""
    Compute a bag directory's Merkle id: sha256 over its sorted lines.

    Each line is terminated by ``\\n``, including the last, so a filename
    containing a newline could not shift the boundaries (rosbag2 does not
    produce such names; the terminator costs nothing and removes the class).
    """
    return _hash_lines(merkle_lines(bag_dir, suffixes))


def file_source_id(path: PathLike) -> str:
    """Compute a single-file source's id: its file key (design section 3)."""
    return file_key(path)


def source_id(path: PathLike) -> str:
    """Identify ``path``, whichever kind of source it is."""
    path = Path(path)
    if path.is_dir():
        return bag_source_id(path)
    return file_source_id(path)


def _hash_lines(lines: Iterable[str]) -> str:
    digest = hashlib.sha256()
    for line in sorted(lines):
        digest.update((line + '\n').encode('utf-8'))
    return digest.hexdigest()
