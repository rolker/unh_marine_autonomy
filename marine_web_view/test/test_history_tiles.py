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


"""Guard the history publisher's pure half.

The raster stages need GDAL and a survey-sized export, so they are exercised
by running the script; what is pinned here is the arithmetic and the
bookkeeping that decide what the page is served and what gets deleted from the
bucket -- the two places where being quietly wrong is expensive.
"""

import importlib.util
import json
import os

import numpy as np

import pytest

_SCRIPT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    'scripts', 'publish_history_tiles.py')


def _load_script():
    """Import publish_history_tiles.py by path.

    Safe to import: GDAL and boto3 are both reached for inside functions, so
    module import needs nothing but numpy -- which is what lets this file run
    in CI on a host with neither.
    """
    spec = importlib.util.spec_from_file_location('publish_history_tiles',
                                                  _SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


SCRIPT = _load_script()


def test_the_mute_commutes_with_averaging():
    """The whole pipeline shape depends on this.

    gdal2tiles builds every overview level by averaging the level below it.
    Muting the source once and tiling the result is only equivalent to muting
    every tile at every level if the mute is affine -- which is why it is a
    luminance blend and a scale, and not a gamma, a clamp, or an HSV round
    trip. Break that and the overview levels are muted differently from the
    native one: the same ground changes colour as you zoom.
    """
    rng = np.random.default_rng(20260827)
    block = rng.integers(0, 256, size=(4, 4, 3)).astype(np.float32)

    muted_then_averaged = SCRIPT.mute_rgb(block).reshape(-1, 3).mean(axis=0)
    averaged_then_muted = SCRIPT.mute_rgb(block.reshape(-1, 3).mean(axis=0))

    assert np.allclose(muted_then_averaged, averaged_then_muted, atol=1e-3)


def test_a_zero_saturation_mute_is_greyscale_before_the_tint():
    """Saturation 0 must leave no hue difference for the tint to act on.

    This is the knob an operator reaches for when the history layer is still
    competing with live coverage, so it has to actually reach neutral rather
    than merely desaturate a long way.
    """
    colours = np.array([[255., 0., 0.], [0., 255., 0.], [0., 0., 255.]])
    muted = SCRIPT.mute_rgb(colours, saturation=0.0, lighten=0.0,
                            tint=(1.0, 1.0, 1.0))
    for row in muted:
        assert np.allclose(row, row[0]), (
            'saturation 0 left {} with a hue'.format(row))


def test_the_mute_warms_rather_than_cools():
    """The layer's whole job is to oppose the live ramp on the hue axis.

    Live coverage is drawn from a blue-green ramp. If the mute ever drifts
    cool, the two layers stop being distinguishable by colour and the operator
    is back to reading the footprint outline to tell today from last Tuesday.
    """
    grey = np.array([[128., 128., 128.]])
    muted = SCRIPT.mute_rgb(grey)[0]
    assert muted[0] > muted[2], (
        'a neutral input came out cool ({}): the tint no longer opposes the '
        'live ramp'.format(muted))


def test_the_zoom_range_rounds_to_the_nearest_level():
    """Rounding up buys a level of empty magnification and 4x the tiles."""
    # A 1 m grid at 43 N is ~1.37 Web Mercator m/px, which is z17's 1.19 and
    # z16's 2.39 -- nearest is 17.
    assert SCRIPT.zoom_range(1.37) == (13, 17)
    # Just below z16's own resolution: still 16, not 17.
    assert SCRIPT.zoom_range(2.30)[1] == 16


def test_the_zoom_range_is_clamped_at_both_ends():
    """A very fine or very coarse mosaic must not drive the tile count.

    The ceiling is the one that costs money -- each level is 4x the tiles --
    and the floor is what keeps the layer from being hidden at every zoom an
    operator actually uses.
    """
    assert SCRIPT.zoom_range(0.001)[1] == SCRIPT.ZOOM_CEILING
    assert SCRIPT.zoom_range(100000.0)[1] == SCRIPT.ZOOM_FLOOR
    with pytest.raises(ValueError):
        SCRIPT.zoom_range(0.0)


def test_nothing_is_removed_when_the_previous_index_is_unreadable():
    """load_index() returns [] for both "absent" and "could not read".

    That collapse is deliberate and this pins the safe direction: an empty
    previous index must produce an empty removal plan. The opposite -- an
    unreadable index treated as "the bucket holds nothing I recognise" --
    would delete a live pyramid on one transient GET failure.
    """
    assert SCRIPT.plan_removals([], ['live/history/15/1/2.png']) == []


def test_tiles_the_new_pyramid_no_longer_covers_are_removed():
    """A published tile outlives the mosaic that produced it.

    The page paints a miss transparent, so ground dropped from the mosaic
    keeps being served from the old render with nothing to say it is stale.
    A static pyramid has no next pass to heal in, so the removal has to happen
    across runs.
    """
    previous = ['live/history/15/1/1.png', 'live/history/15/1/2.png']
    current = ['live/history/15/1/2.png', 'live/history/15/1/3.png']
    assert SCRIPT.plan_removals(previous, current) == [
        'live/history/15/1/1.png']


def test_labels_default_to_the_basename_and_never_to_the_mtime():
    """An export written the morning after a survey carries the wrong date.

    Dating the layer from the file's mtime would publish that wrong date
    confidently, over ground that was run the day before. The basename is
    merely unhelpful, which is the better failure.
    """
    labels = SCRIPT.resolve_labels(
        ['/data/Shoals Day1_1m_CUBE_ITRF2020.tif'], [])
    assert labels == ['Shoals Day1_1m_CUBE_ITRF2020']


def test_a_label_list_that_does_not_match_the_sources_is_refused():
    """Labels are matched by POSITION, so a short list mislabels a day.

    Silently zipping to the shorter list would publish the second day's
    coverage under the first day's date, which is worse than not running.
    """
    with pytest.raises(ValueError):
        SCRIPT.resolve_labels(['a.tif', 'b.tif'], ['2026-08-25'])
    assert SCRIPT.resolve_labels(['a.tif', 'b.tif'],
                                 ['2026-08-25', '2026-08-26']) == [
        '2026-08-25', '2026-08-26']


def test_the_manifest_carries_leaflet_bounds_not_geojson_order():
    """[[south, west], [north, east]] -- the reverse of GeoJSON's [lon, lat].

    Both orders are live on this page: state_renderer publishes GeoJSON and
    the manifest feeds Leaflet. Getting this one backwards puts the survey in
    the Indian Ocean, and nothing errors.
    """
    bounds = [[42.98, -70.71], [43.07, -70.60]]
    manifest = SCRIPT.build_manifest(13, 17, 218, [
        {'file': 'day1.tif', 'label': '2026-08-25'}], bounds, 1787832866.0)
    (south, west), (north, east) = manifest['bounds']
    assert south < north and west < east
    assert -90 <= south <= 90 and -180 <= west <= 180


def test_the_manifest_stays_small_enough_to_poll():
    """Every viewer fetches this. The key index belongs in its own object.

    Un-publishing needs the full list of published keys -- hundreds of
    strings. Carried in the manifest, that is tens of kilobytes served to
    every viewer every poll to communicate four numbers.
    """
    sources = [{'file': 'day{}.tif'.format(i), 'label': '2026-08-{:02d}'
                .format(i)} for i in range(1, 15)]
    manifest = SCRIPT.build_manifest(13, 17, 5000, sources,
                                     [[42.9, -70.8], [43.1, -70.5]],
                                     1787832866.0)
    payload = json.dumps(manifest)
    assert len(payload) < 4096, (
        'the manifest is {} bytes; something bulky has been added to the '
        'object every viewer polls'.format(len(payload)))
    assert 'keys' not in manifest
    assert manifest['labels'] == [s['label'] for s in sources]


def test_the_tile_and_manifest_cache_headers_do_not_drift():
    """A day that is over does not change; the manifest is how a new one lands.

    Cache the manifest as hard as the tiles and a viewer with the page open
    does not see tomorrow's survey until they force a reload. Cache the tiles
    as briefly as the manifest and every pan re-fetches a pyramid that has not
    changed since the day it was published.
    """
    assert 'max-age=86400' in SCRIPT.TILE_EXTRA_ARGS['CacheControl']
    assert 'max-age=300' in SCRIPT.META_EXTRA_ARGS['CacheControl']
    assert SCRIPT.TILE_EXTRA_ARGS['ContentType'] == 'image/png'


def test_the_prefix_stays_inside_what_the_boat_credential_can_write():
    """p11-renderer is scoped to s3:PutObject on live/* and nothing else.

    Move this pyramid to a sibling top-level prefix and every run needs an
    admin profile -- which is exactly the position refresh_chart_tiles.py is
    in with tiles/, and the reason it cannot run under the boat's own
    credential.
    """
    assert SCRIPT.PREFIX.startswith('live/')
    assert SCRIPT.META_KEY.startswith('live/')
    assert SCRIPT.INDEX_KEY.startswith('live/')
