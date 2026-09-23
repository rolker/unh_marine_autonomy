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
A stand-in for ``build_depth_overview_parent``, for the workflow tests.

It keeps the real tool's command-line CONTRACT -- ``--list-parents`` output
with children, ``--prune``, one parent per invocation writing a 4-band tile and
its per-tile record -- over a toy quadtree (parent = row//2, col//2) instead of
GGGS, and appends what it did to ``$FAKE_TOOL_LOG`` so a test can count the
builds a run made. The real tool's semantics are pinned by the C++ tests
(``test_depth_overview_multiband.cpp``); what this exercises is the DAG around
it. Not a test module (no ``test_`` prefix), so pytest does not collect it.
"""

import hashlib
import json
import os
from pathlib import Path
import re
import sys

from osgeo import gdal

gdal.UseExceptions()
NAME = re.compile(r'(\d+)_(\d+)_(\d+)\.tif')


def _log(line):
    with open(os.environ['FAKE_TOOL_LOG'], 'a') as handle:
        handle.write(line + '\n')


def _tiles(directory, level):
    found = []
    if directory.is_dir():
        for path in directory.iterdir():
            match = NAME.fullmatch(path.name)
            if match and int(match.group(1)) == level:
                found.append(tuple(int(g) for g in match.groups()))
    return sorted(found)


def _name(level, row, col):
    return f'{level}_{row}_{col}.tif'


def _children(layer, level, row, col):
    out = []
    for r in (2 * row, 2 * row + 1):
        for c in (2 * col, 2 * col + 1):
            name = _name(level + 1, r, c)
            if (layer / name).exists():
                out.append(name)
            elif (layer / 'overviews' / name).exists():
                out.append('overviews/' + name)
    return out


def list_parents(layer, level):
    parents = set()
    for directory in (layer, layer / 'overviews'):
        for _, row, col in _tiles(directory, level + 1):
            if not (layer / _name(level, row // 2, col // 2)).exists():
                parents.add((level, row // 2, col // 2))
    for parent in sorted(parents):
        print('\t'.join([_name(*parent)[:-4]] + _children(layer, *parent)))


def prune(layer, level):
    for _, row, col in _tiles(layer / 'overviews', level):
        tile = layer / 'overviews' / _name(level, row, col)
        if (layer / tile.name).exists() or not _children(layer, level, row,
                                                         col):
            for path in (tile, tile.with_suffix('.json'),
                         tile.with_name(tile.name + '.fp')):
                path.unlink(missing_ok=True)
            _log(f'prune {tile.name[:-4]}')
            print(tile.name[:-4])


def build(layer, level, row, col):
    children = _children(layer, level, row, col)
    if (layer / _name(level, row, col)).exists() or not children:
        return
    digest = hashlib.sha256()
    for child in children:
        digest.update((layer / child).read_bytes())
    value = int(digest.hexdigest()[:6], 16) / 1000.0
    overviews = layer / 'overviews'
    overviews.mkdir(exist_ok=True)
    final = overviews / _name(level, row, col)
    tmp = overviews / f'.{final.name}.{os.getpid()}.tmp.tif'
    dataset = gdal.GetDriverByName('GTiff').Create(
        str(tmp), 4, 4, 4, gdal.GDT_Float64)
    dataset.SetGeoTransform((-70.0, 0.001, 0.0, 42.1, 0.0, -0.001))
    dataset.GetRasterBand(1).Fill(value)
    dataset = None
    tmp.replace(final)
    final.with_suffix('.json').write_text(json.dumps({
        'schema': 'depth-overview-tile/1', 'geometric_error_m': 2.0 ** level,
        'bands': ['min', 'mean', 'count', 'sigma'],
        'sigma_fold': 'undecided', 'sigma_band_written': False,
        'children_used': len(children), 'children': children}))
    _log(f'build {final.name[:-4]}')


def main(argv):
    if argv[0] == '--list-parents':
        list_parents(Path(argv[1]), int(argv[2]))
    elif argv[0] == '--prune':
        prune(Path(argv[1]), int(argv[2]))
    else:
        build(Path(argv[0]), *(int(a) for a in argv[1:4]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
