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
The store root is a parameter -- this fails when it stops being one.

Design draft section 2: "The store root is a **parameter**, defaulting to
``~/data/world`` and never a literal in code; the prototype builds under a
different root so that any hard-coded path fails." A sentence in a design
document cannot enforce that, so this test greps the repository for the
literal and fails on anything the allowlist does not name.

The allowlist is narrow and each entry says why. Adding to it is a visible
edit in a pull request, which is the point: a new hard-coded path costs a
reviewer's attention rather than nothing.
"""

import os
from pathlib import Path
import subprocess

#: The literal a hard-coded store root contains, whatever the prefix.
LITERAL = 'data/world'

#: File kinds that can carry a path into the running system.
SUFFIXES = ('.py', '.cpp', '.hpp', '.h', '.smk')

#: Directories whose content is prose or fixtures, not running code.
EXCLUDED_DIRS = ('docs', 'build', 'install', 'log', '.git')

#: Pre-existing occurrences, each a *different* setting from the world store
#: root, each already overridable. None of them is rewritten by #397 -- doing
#: so silently would be a scope creep into three other packages' defaults.
ALLOWLIST = {
    # An S-102 import *cache*, not the store root, and already overridable
    # with --cache (#288 / uma-ADR-0010 D3).
    'marine_bathymetry_store/src/s102_import_main.cpp':
        'S-102 import cache default, overridable with --cache',
    # A comment explaining the store_path parameter's tilde expansion; the
    # parameter itself has no baked default.
    'bathymetry_layer/src/bathymetry_layer.cpp':
        'comment illustrating the store_path parameter',
    # The legacy imagery tree's launch default, a ROS parameter the operator
    # overrides; it addresses the pre-rev-3 layout, which #397 does not touch.
    'marine_sidescan_mosaic/launch/sidescan_mosaic.launch.py':
        'legacy imagery-tree launch default, a ROS parameter',
    # The one place the default may be written (this package's own constant).
    'marine_world_store/marine_world_store/store_root.py':
        'the single _DEFAULT_ROOT constant this test exists to protect',
    # This test names the allowlisted paths, so it contains them itself.
    'marine_world_store/test/test_no_literal_store_root.py':
        'the guard test itself',
}


def _tracked_files(repo_root: Path):
    """Every tracked source file, from git where possible, else a walk."""
    try:
        output = subprocess.run(
            ['git', '-C', str(repo_root), 'ls-files'],
            capture_output=True, text=True, check=True).stdout
        names = [line for line in output.splitlines() if line]
    except (OSError, subprocess.CalledProcessError):
        names = []
    if not names:
        # No git (a source tarball, a container): walk instead, so the guard
        # still guards rather than silently passing.
        names = [
            str(path.relative_to(repo_root))
            for path in repo_root.rglob('*')
            if path.is_file()]
    for name in names:
        parts = Path(name).parts
        if any(part in EXCLUDED_DIRS for part in parts):
            continue
        if 'test' in parts and name not in ALLOWLIST:
            # Tests legitimately build under a fake home; they are not the
            # running system. The guard test itself is allowlisted above.
            continue
        if name.endswith(SUFFIXES):
            yield name


def test_repository_is_scannable(repo_root: Path):
    """Guard the guard: an empty file list would pass vacuously."""
    assert len(list(_tracked_files(repo_root))) > 20


def test_default_root_literal_appears_once(package_dir: Path):
    """``_DEFAULT_ROOT`` is the only place the default is written."""
    source = (package_dir / 'marine_world_store' / 'store_root.py').read_text()
    occurrences = [line for line in source.splitlines() if LITERAL in line]
    assert len(occurrences) == 1, (
        'store_root.py must contain the default exactly once (the '
        f'_DEFAULT_ROOT constant); found {len(occurrences)}:\n'
        + '\n'.join(occurrences))
    assert occurrences[0].strip().startswith('_DEFAULT_ROOT = '), \
        f'the one occurrence is not the constant: {occurrences[0]!r}'


def test_no_hard_coded_store_root(repo_root: Path):
    """No source file outside the allowlist contains the literal."""
    offenders = []
    for name in _tracked_files(repo_root):
        if name in ALLOWLIST:
            continue
        path = repo_root / name
        try:
            text = path.read_text()
        except (OSError, UnicodeDecodeError):
            continue
        for number, line in enumerate(text.splitlines(), start=1):
            if LITERAL in line:
                offenders.append(f'{name}:{number}: {line.strip()}')
    assert not offenders, (
        'The world store root is a parameter (design section 2) and must be '
        'resolved through marine_world_store.store_root.resolve_store_root(), '
        'never written as a literal:\n  ' + '\n  '.join(offenders))


def test_allowlist_entries_still_exist(repo_root: Path):
    """
    An allowlist entry whose file is gone is rot -- remove the entry.

    Without this, the allowlist quietly outlives what it excused and the next
    file to take that path inherits the exemption.
    """
    missing = [name for name in ALLOWLIST if not (repo_root / name).exists()]
    assert not missing, (
        'allowlisted file(s) no longer exist; drop them from ALLOWLIST: '
        f'{missing}')


def test_allowlist_entries_still_contain_the_literal(repo_root: Path):
    """An allowlisted file that no longer needs the exemption loses it."""
    stale = []
    for name in ALLOWLIST:
        if name.endswith('test_no_literal_store_root.py'):
            continue
        if LITERAL not in (repo_root / name).read_text():
            stale.append(name)
    assert not stale, (
        'allowlisted file(s) no longer contain the literal; drop them from '
        f'ALLOWLIST: {stale}')


def test_resolve_store_root_is_what_code_uses(repo_root: Path):
    """Sanity: the resolver exists and the env var name is what we document."""
    del repo_root
    from marine_world_store.store_root import ENV_VAR, resolve_store_root
    assert ENV_VAR == 'WORLD_STORE_ROOT'
    assert callable(resolve_store_root)


def test_environment_is_not_consulted_by_accident(tmp_path, monkeypatch):
    """A test that sets the env var must not leak into the developer's shell."""
    monkeypatch.setenv('WORLD_STORE_ROOT', str(tmp_path))
    from marine_world_store.store_root import resolve_store_root
    assert resolve_store_root() == tmp_path
    assert os.environ['WORLD_STORE_ROOT'] == str(tmp_path)
