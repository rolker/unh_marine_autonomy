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
Shared fixtures: where this package and its repository are on disk.

The tests run from two places -- plain ``pytest`` in the package directory and
``colcon test``, which runs from the layer workspace -- so no test may assume
the current working directory.
"""

from pathlib import Path
import sys

import pytest

# Both runners must be able to import the package under test: plain pytest
# from the package directory, and colcon test, which runs from the layer
# workspace. Neither puts the package directory on sys.path by itself.
_PACKAGE_DIR = Path(__file__).resolve().parent.parent
if str(_PACKAGE_DIR) not in sys.path:
    sys.path.insert(0, str(_PACKAGE_DIR))


@pytest.fixture(scope='session')
def package_dir() -> Path:
    """Locate the ``marine_world_store`` package directory."""
    here = Path(__file__).resolve().parent.parent
    assert (here / 'package.xml').is_file(), \
        f'{here} does not look like the package directory'
    return here


@pytest.fixture(scope='session')
def module_dir(package_dir: Path) -> Path:
    """Locate the importable ``marine_world_store/`` package inside it."""
    return package_dir / 'marine_world_store'


@pytest.fixture(scope='session')
def repo_root(package_dir: Path) -> Path:
    """Locate the ``unh_marine_autonomy`` checkout this package lives in."""
    return package_dir.parent
