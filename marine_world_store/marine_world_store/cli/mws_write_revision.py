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
``mws_write_revision`` -- append one record to ``revisions/``.

Reads a small YAML or JSON description and writes the append-only revision
Item it describes (design section 2). The description is a file rather than a
pile of flags because a revision carries the reviewer's evidence, and evidence
does not fit on a command line.

The description's fields are the arguments of
:func:`marine_world_store.revisions.build_revision`; the package README
carries a worked example.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Optional, Sequence

from marine_world_store import revisions
from marine_world_store.cli._common import (
    add_store_root_argument, resolved_root, run,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the command line."""
    parser = argparse.ArgumentParser(
        prog='mws_write_revision', description=__doc__.splitlines()[0])
    parser.add_argument('description',
                        help='YAML or JSON file describing the revision')
    add_store_root_argument(parser)
    parser.add_argument('--dry-run', action='store_true',
                        help='build and print the record, write nothing')
    return parser


def load_description(path: Path) -> dict:
    """
    Read the description file, JSON or YAML.

    :raises ValueError: when the document is not a mapping -- a list of
        revisions would be written as one unreadable record.
    """
    text = path.read_text()
    if path.suffix.lower() == '.json':
        document = json.loads(text)
    else:
        import yaml
        document = yaml.safe_load(text)
    if not isinstance(document, dict):
        raise ValueError(
            f'{path}: expected a mapping describing one revision, got '
            f'{type(document).__name__}')
    return document


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Build the record and append it."""
    args = build_parser().parse_args(argv)
    description = load_description(Path(args.description).expanduser())
    unknown = sorted(set(description) - {
        'kind', 'applies_to', 'parameters', 'evidence', 'reviewer',
        'valid_from', 'valid_to', 'notes'})
    if unknown:
        raise ValueError(
            f'unknown field(s) in the description: {unknown}. A field nobody '
            'reads would silently not be part of the record.')
    revision = revisions.build_revision(**description)
    print(f'revision id: {revision["id"]}')
    if args.dry_run:
        print(json.dumps(revision, indent=2, sort_keys=True))
        return 0
    root = resolved_root(args)
    path = revisions.write_revision(root.path, revision)
    print(f'wrote: {path}')
    return 0


def console_main() -> int:
    """Entry point (``console_scripts``)."""
    return run(main)


if __name__ == '__main__':
    raise SystemExit(console_main())
