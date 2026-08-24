#!/usr/bin/env python3

# Copyright 2026 Roland Arsenault
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
#    * Neither the name of the Roland Arsenault nor the names of its
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

"""Pin the local (dry-run) output path and the prefix it is built from.

The preview directory is served straight off disk by a plain http.server, so
what lands in it matters: a temp file left behind is served, a 0600 file may
not be, and a prefix carrying `..` writes outside the directory entirely.
None of that shows up in a normal run -- the tiles look right either way.
"""

import os
import stat
import threading

from marine_web_view.coverage_renderer import _safe_prefix
from marine_web_view.coverage_renderer import CoverageRenderer


class _Logger:
    """Collect log lines instead of printing them."""

    def __init__(self):
        self.lines = []

    def warn(self, message, **kwargs):
        """Record a line."""
        self.lines.append(message)

    info = warn
    debug = warn
    error = warn


class _Writer:
    """Minimal stand-in exposing the local write path."""

    def __init__(self, local_dir):
        self.local_dir = local_dir
        self.dry_run = True
        self._failures = 0
        self._lock = threading.Lock()
        self._logger = _Logger()

    def get_logger(self):
        """Return the collecting logger."""
        return self._logger

    _write_local = CoverageRenderer._write_local


def test_a_prefix_cannot_climb_out_of_the_directory():
    """`..` in a parameter would escape the served preview directory."""
    assert _safe_prefix('live/coverage') == 'live/coverage'
    assert _safe_prefix('/live/coverage/') == 'live/coverage'
    assert _safe_prefix('../../etc') == 'etc'
    assert _safe_prefix('live/../../../coverage') == 'live/coverage'
    # Empty is what the node refuses at startup: every key would then start
    # with a slash, os.path.join would discard local_dir, and every write
    # would fail and retry forever.
    assert _safe_prefix('') == ''
    assert _safe_prefix('live//coverage') == 'live/coverage'


def test_a_written_tile_is_readable_by_a_static_server(tmp_path):
    """Temp files are created 0600; the sibling artifacts are 0644."""
    node = _Writer(str(tmp_path))
    assert node._write_local(b'payload', 'live/coverage/15/1/2.png')
    path = tmp_path / 'live' / 'coverage' / '15' / '1' / '2.png'
    assert path.read_bytes() == b'payload'
    mode = stat.S_IMODE(os.stat(path).st_mode)
    assert mode & stat.S_IROTH, 'oct({}) is not world-readable'.format(mode)


def test_no_temp_files_are_left_in_a_served_directory(tmp_path):
    """A leftover temp file is served as a partial tile."""
    node = _Writer(str(tmp_path))
    node._write_local(b'payload', 'live/coverage/15/1/2.png')
    directory = tmp_path / 'live' / 'coverage' / '15' / '1'
    assert [entry.name for entry in directory.iterdir()] == ['2.png']


def test_a_traversing_key_is_refused(tmp_path):
    """Belt and braces behind the prefix validation."""
    node = _Writer(str(tmp_path / 'web'))
    os.makedirs(str(tmp_path / 'web'))
    assert not node._write_local(b'payload', '../escaped.png')
    assert not (tmp_path / 'escaped.png').exists()
    assert node._logger.lines
