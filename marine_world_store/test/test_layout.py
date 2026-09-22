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

"""The rev-3 directory layout, and the typos it refuses."""

from pathlib import Path

from marine_world_store import layout
from marine_world_store.layout import LayoutError, Origin, Quantity, State
import pytest


def test_quantity_directory_shape(tmp_path):
    """``<root>/<quantity>/<state>/<origin>/`` (design section 5)."""
    assert layout.quantity_dir(
        tmp_path, Quantity.DEPTHS, State.REVIEWED, Origin.SURVEYED) == \
        tmp_path / 'depths' / 'reviewed' / 'surveyed'


def test_strings_are_accepted_when_they_are_exact(tmp_path):
    """A CLI passes strings; the exact spellings are the enum values."""
    assert layout.quantity_dir(tmp_path, 'depths', 'published', 'imported') == \
        tmp_path / 'depths' / 'published' / 'imported'


@pytest.mark.parametrize('state', ['reviwed', 'Reviewed', 'curated', ''])
def test_a_mistyped_state_is_an_error_not_a_new_directory(tmp_path, state):
    """The failure mode this module exists to prevent."""
    with pytest.raises(LayoutError):
        layout.quantity_dir(tmp_path, Quantity.DEPTHS, state, Origin.SURVEYED)


@pytest.mark.parametrize('origin', ['survey', 'ours', 'external'])
def test_a_mistyped_origin_is_an_error(tmp_path, origin):
    """Same for origin, which is a directory and not only an Item field."""
    with pytest.raises(LayoutError):
        layout.quantity_dir(tmp_path, Quantity.DEPTHS, State.DRAFT, origin)


def test_the_old_rung_names_map_onto_cells(tmp_path):
    """Map the old rung name: processed is reviewed/surveyed."""
    assert layout.quantity_dir(
        tmp_path, Quantity.DEPTHS, State.REVIEWED, Origin.SURVEYED).parts[-2:] \
        == ('reviewed', 'surveyed')


def test_sources_and_revisions_have_no_axes(tmp_path):
    """They are not quantity stores."""
    assert layout.sources_dir(tmp_path) == tmp_path / 'sources'
    assert layout.revisions_dir(tmp_path) == tmp_path / 'revisions'


def test_trajectories_and_observations_are_surveyed_only(tmp_path):
    """Design section 5 says so explicitly."""
    assert layout.trajectories_dir(tmp_path) == \
        tmp_path / 'trajectories' / 'surveyed'
    assert layout.observations_dir(tmp_path) == \
        tmp_path / 'observations' / 'surveyed'
    with pytest.raises(LayoutError):
        layout.trajectories_dir(tmp_path, Origin.IMPORTED)
    with pytest.raises(LayoutError):
        layout.observations_dir(tmp_path, Origin.IMPORTED)


def test_tile_filename_matches_the_cxx_spelling():
    """One naming convention, shared with marine_tiled_raster_store."""
    assert layout.tile_filename(12, 34, 56) == '12_34_56.tif'


@pytest.mark.parametrize('args', [(-1, 0, 0), (0, -1, 0), (1.5, 0, 0)])
def test_tile_filename_refuses_impossible_indices(args):
    """A negative or fractional index is a caller bug, not a filename."""
    with pytest.raises(LayoutError):
        layout.tile_filename(*args)


@pytest.mark.parametrize('name,expected', [
    ('12_34_56.tif', (12, 34, 56)),
    ('0_0_0.tif', (0, 0, 0)),
    ('coverage.json', None),
    ('collection.json', None),
    ('12_34.tif', None),
    ('12_34_56.tif.tmp', None),
    ('a_b_c.tif', None),
])
def test_parse_tile_filename_is_tolerant(name, expected):
    """A layer directory holds non-tile files; reading one is not an error."""
    assert layout.parse_tile_filename(name) == expected


def test_tiles_in_dir_is_ordered_and_ignores_the_rest(tmp_path):
    """GGGS order (level, row, column), non-tiles skipped."""
    for name in ('12_2_1.tif', '12_1_9.tif', '11_9_9.tif', 'coverage.json'):
        (tmp_path / name).write_text('')
    assert [p.name for p in layout.tiles_in_dir(tmp_path)] == [
        '11_9_9.tif', '12_1_9.tif', '12_2_1.tif']


def test_tiles_in_dir_of_a_missing_directory_is_empty(tmp_path):
    """A layer that does not exist holds no tiles; that is not an error."""
    assert list(layout.tiles_in_dir(tmp_path / 'nope')) == []


def test_sidecar_paths(tmp_path):
    """The overview sidecar and manifest names come from the C++ side."""
    layer = Path(tmp_path) / 'depths'
    assert layout.overviews_dir(layer) == layer / 'overviews'
    assert layout.coverage_manifest_path(layer) == layer / 'coverage.json'
    assert layout.collection_path(layer) == layer / 'collection.json'
