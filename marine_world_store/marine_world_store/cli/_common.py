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
Argument-parsing pieces every ``mws_*`` CLI shares.

Kept here rather than copied per tool so that ``--store-root`` means the same
thing, and reports where it came from the same way, in all of them.
"""

from __future__ import annotations

import argparse
import sys
from typing import Optional, Sequence

from marine_world_store.store_root import (
    ENV_VAR, resolve_store_root_verbose, StoreRoot,
)


def add_store_root_argument(parser: argparse.ArgumentParser) -> None:
    """Add the ``--store-root`` flag, documenting the precedence."""
    parser.add_argument(
        '--store-root', default=None, metavar='DIR',
        help=(f'world store root. Precedence: this flag, then ${ENV_VAR}, '
              'then store_root: in ~/.config/marine_world_store/config.yaml, '
              'then the documented default.'))


def resolved_root(args: argparse.Namespace) -> StoreRoot:
    """Resolve ``--store-root`` and report which mechanism decided it."""
    root = resolve_store_root_verbose(args.store_root)
    print(f'store root: {root.path}  (from {root.source})')
    return root


def stac_catalog():
    """
    Import the catalog writer, or say what is missing and how to fix it.

    ``pystac`` resolves through the repo-root ``rosdep.yaml`` local key, so a
    checkout that has not had ``rosdep install`` run yet does not have it. The
    parts of every tool that write nothing (``--dry-run``, computing an id)
    must keep working there, which is why this import is not at module scope.
    """
    try:
        from marine_world_store import stac_catalog as module
    except ImportError as exc:
        raise RuntimeError(
            'writing Items needs pystac (rosdep key python3-pystac, declared '
            'in package.xml and resolved by the repo-root rosdep.yaml). Run '
            f'`rosdep install` for this repo, or use --dry-run. ({exc})'
        ) from exc
    return module


def run(main, argv: Optional[Sequence[str]] = None) -> int:
    """
    Run ``main``, turning an expected failure into a message and a 1.

    Expected failures are the ones this package raises deliberately (a bad
    store root, an unidentifiable source, an Item that breaks the contract);
    they are a CLI's normal "no" and need no traceback. Anything else keeps
    its traceback, because an unexpected exception is a bug report.
    """
    try:
        return main(argv)
    except (ValueError, RuntimeError, OSError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 1
