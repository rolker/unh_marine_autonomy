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
Publish a file whole, durably, and without colliding with another writer.

Every writer in this package publishes by write-beside-then-rename, so a reader
sees the old file or the new one and never half of either. Two things that
pattern needs and the first versions of it here did not have:

* **A temporary name nobody else uses.** A fixed ``<name>.tmp`` is shared by
  every writer of ``<name>``: two concurrent runs write into the same
  temporary, and whichever renames first can publish the OTHER run's
  half-written bytes. The temporary here is unique per call
  (:func:`tempfile.mkstemp`), in the destination's own directory so the rename
  stays within one filesystem and therefore atomic.
* **fsync before the rename, and of the directory after it.** ``rename(2)`` is
  atomic, not durable: after a crash, a renamed file whose data never reached
  the disk can be present and empty. Syncing the data first, then the
  directory entry, is what makes "the new file" mean the new CONTENT.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import tempfile
from typing import Callable, Union

PathLike = Union[str, Path]

#: The mode ``open(path, 'w')`` asks for, before the umask is applied.
_PUBLISHED_MODE = 0o666


def _current_umask() -> int:
    """Return the process umask (reading it means setting it; put it back)."""
    mask = os.umask(0o022)
    os.umask(mask)
    return mask


def _fsync_directory(directory: Path) -> None:
    """Make a rename in ``directory`` durable (best effort where unsupported)."""
    try:
        fd = os.open(str(directory), os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    except OSError:
        # Some filesystems (and platforms) refuse fsync on a directory; the
        # rename is still atomic there, only its durability is the fs's call.
        pass
    finally:
        os.close(fd)


def publish(path: PathLike, fill: Callable[[Path], None]) -> Path:
    """
    Publish ``path`` atomically: ``fill`` writes a private temporary.

    :param fill: called with the temporary's path; it writes the content. If it
        raises, the temporary is removed and nothing is published.
    :returns: ``path``.
    """
    path = Path(path)
    fd, name = tempfile.mkstemp(
        prefix=f'.{path.name}.', suffix='.tmp', dir=str(path.parent))
    tmp = Path(name)
    try:
        # mkstemp creates the temporary 0600, which is right for a scratch
        # file and wrong for a published one: every Item, Collection and
        # manifest would be owner-only, unreadable to the renderers, CAMP, a
        # container run as another uid or a NAS sync. Give it the mode a plain
        # open() would have given the file -- 0666 less the umask. (A fill
        # that copies metadata, as copy_file does, may set its own.)
        os.fchmod(fd, _PUBLISHED_MODE & ~_current_umask())
        os.close(fd)
        fd = -1
        fill(tmp)
        with open(tmp, 'rb') as handle:
            os.fsync(handle.fileno())
        os.replace(tmp, path)
    except BaseException:
        if fd >= 0:
            os.close(fd)
        tmp.unlink(missing_ok=True)
        raise
    _fsync_directory(path.parent)
    return path


def write_text(path: PathLike, text: str) -> Path:
    """Publish ``text`` as ``path`` (see :func:`publish`)."""
    return publish(path, lambda tmp: tmp.write_text(text))


def copy_file(source: PathLike, path: PathLike,
              check: Callable[[Path], None] = lambda tmp: None) -> Path:
    """
    Publish a copy of ``source`` as ``path``, metadata included.

    :param check: called on the temporary before it is published; raise to
        refuse the copy (the byte-identical check lives here, so a copy that
        fails it is never visible under ``path`` at all).
    """
    def fill(tmp: Path) -> None:
        shutil.copy2(source, tmp)
        check(tmp)
    return publish(path, fill)
