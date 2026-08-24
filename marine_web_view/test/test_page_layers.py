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

"""Guard that every map layer the page defines is actually added to the map.

A Leaflet layer class can be defined, configured, commented and completely
orphaned: nothing errors, no console warning appears, and fetching one of its
tiles by hand still succeeds -- so the layer looks fine from every angle
except the only one that matters.

That happened. The hillshade layer in #341 was defined and never instantiated,
because one of two string substitutions silently did not match. It shipped to
production and was reported as working, having been "verified" by fetching a
relief tile directly, which proved the URL resolved and nothing about the map.

There is no JS test runner here, so this is deliberately a crude textual
check. It catches the specific failure that occurred rather than trying to
understand the page.
"""

import os
import re

WEB = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'web')


def _page():
    """Return the text of the web page."""
    with open(os.path.join(WEB, 'index.html')) as handle:
        return handle.read()


def test_every_layer_class_is_instantiated():
    """A defined-but-unused L.TileLayer subclass is a dead layer."""
    page = _page()
    defined = set(re.findall(r'const\s+([A-Z]\w*)\s*=\s*L\.TileLayer\.extend',
                             page))
    assert defined, 'no layer classes found -- has the page been restructured?'
    for name in sorted(defined):
        assert re.search(r'\bnew\s+' + name + r'\s*\(', page), (
            '{} is defined but never instantiated -- the layer will not '
            'appear on the map, silently'.format(name))


# Every way this page builds a tile layer. `new L.TileLayer(` is here because
# it was NOT matched by the original `new [A-Z]\w*(` pattern -- the coverage
# layer was built through it and went unguarded by the very test written to
# catch an orphaned layer.
_CONSTRUCTIONS = r'\bnew\s+L\.TileLayer\s*\(|\bL\.tileLayer\s*\(|\bnew\s+(?:Bathy|Relief)\s*\('


def _code(page):
    """Return the page with comment text blanked out, offsets preserved.

    The scan below tracks quotes, and the page's own prose is full of
    apostrophes ("Leaflet's", "layer's"). Left in, each one opens a phantom
    string and the bracket depth after it is meaningless -- which is how the
    statement window silently ran to the end of the file.
    """
    out = list(page)
    index = 0
    end = len(page)
    while index < end:
        char = page[index]
        if char in '\'"`':
            quote = char
            index += 1
            while index < end:
                if page[index] == '\\':
                    index += 2
                    continue
                if page[index] == quote:
                    break
                index += 1
            index += 1
        elif page.startswith('//', index):
            while index < end and page[index] != '\n':
                out[index] = ' '
                index += 1
        elif page.startswith('/*', index) or page.startswith('<!--', index):
            close = '*/' if page[index + 1] == '*' else '-->'
            stop = page.find(close, index + 2)
            stop = end if stop < 0 else stop + len(close)
            for position in range(index, stop):
                if page[position] != '\n':
                    out[position] = ' '
            index = stop
        else:
            index += 1
    return ''.join(out)


def _statement(page, start):
    """Return just the JavaScript statement that begins at `start`.

    The window a construction site is checked in has to end where its own
    statement ends. Running it to the next construction -- and, for the last
    site, to the end of the page -- let the coverage layer borrow the
    `.addTo(map)` of the trail and hull polylines further down the file. That
    is the exact layer this test exists for, and deleting `.addTo(map)` from
    `buildCoverage()` left the suite green twice.

    Scans forward tracking bracket depth, skipping quoted text (the coverage
    layer's data: URL contains a `;` of its own), and stops at the first `;`
    outside all brackets. Call it on `_code(page)`, not on the raw page.
    """
    depth = 0
    index = start
    end = len(page)
    while index < end:
        char = page[index]
        if char in '\'"`':
            quote = char
            index += 1
            while index < end:
                if page[index] == '\\':
                    index += 2
                    continue
                if page[index] == quote:
                    break
                index += 1
        elif char in '([{':
            depth += 1
        elif char in ')]}':
            depth -= 1
        elif char == ';' and depth <= 0:
            return page[start:index]
        index += 1
    return page[start:]


def test_every_layer_reaches_the_map():
    """Every tile-layer construction must be followed by addTo(map).

    Counting constructions against `.addTo(map)` calls did not bind: the
    comparison was `>=`, and the trail and hull contribute addTo calls of
    their own, so an orphaned tile layer could hide behind them. This checks
    each construction site inside its own statement instead.
    """
    page = _code(_page())
    sites = [m.start() for m in re.finditer(_CONSTRUCTIONS, page)]
    assert len(sites) >= 4, (
        'expected at least imagery, bathymetry, hillshade and coverage; '
        'found {} -- has the page been restructured?'.format(len(sites)))
    for start in sites:
        statement = _statement(page, start)
        assert '.addTo(map)' in statement, (
            'the tile layer constructed at offset {} is never added to the '
            'map -- it will not appear, silently'.format(start))


def test_the_coverage_layer_is_configured_from_the_manifest():
    """The page must not hardcode the zoom the renderer writes.

    The two drifted apart once already: a layer pinned to the wrong native
    zoom requests tiles that were never written, one 403 per tile per pan.
    """
    page = _page()
    assert 'meta.json' in page, 'the coverage manifest is not fetched'
    assert re.search(r'buildCoverage\(\s*meta\.zoom\s*\)', page), (
        'the coverage layer is not built from the manifest zoom')
    assert not re.search(r'COVERAGE_Z\s*=\s*\d', page), (
        'the render zoom is hardcoded in the page again')


def test_a_dead_renderer_is_reported_rather_than_hidden():
    """Every missing tile is painted transparent, total failure included."""
    page = _page()
    assert 'errorTileUrl' in page
    assert "'offline'" in page, (
        'with every missing tile painted transparent, a dead renderer must '
        'be reported from the manifest or it reads as calm water')
    assert re.search(r'id="cov"', page), 'no coverage readout on the page'


def test_expected_layers_are_present():
    """The page must carry imagery, bathymetry, hillshade and coverage."""
    page = _page()
    for marker, description in (
            ('World_Imagery', 'Esri imagery basemap'),
            ('exportImage', 'CCOM bathymetry'),
            ('new Relief', 'hillshade'),
            ('live/coverage/', 'live sonar coverage'),
            ('live/position.geojson', 'vessel position'),
            ('live/track.geojson', 'vessel track')):
        assert marker in page, '{} ({}) missing from the page'.format(
            description, marker)
