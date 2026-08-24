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
    defined = _layer_classes(page)
    assert defined, 'no layer classes found -- has the page been restructured?'
    for name in defined:
        assert re.search(r'\bnew\s+' + name + r'\s*\(', page), (
            '{} is defined but never instantiated -- the layer will not '
            'appear on the map, silently'.format(name))


# Leaflet's two built-in ways of making a tile layer. `new L.TileLayer(` is
# here because it was NOT matched by the original `new [A-Z]\w*(` pattern --
# the coverage layer was built through it and went unguarded by the very test
# written to catch an orphaned layer.
_BUILTIN_CONSTRUCTIONS = (r'\bnew\s+L\.TileLayer\s*\(', r'\bL\.tileLayer\s*\(')

_LAYER_CLASS = r'const\s+([A-Z]\w*)\s*=\s*L\.TileLayer\.extend'


def _layer_classes(page):
    """Return the names of the page's own `L.TileLayer` subclasses."""
    return sorted(set(re.findall(_LAYER_CLASS, page)))


def _constructions(page):
    """Return a regex matching every way this page builds a tile layer.

    Derived from the subclasses the page actually defines rather than from a
    hardcoded `Bathy|Relief`: a list spelled out here goes stale the moment
    someone adds a fourth layer, and the new one would then escape the addTo
    check silently -- which is the exact class of failure (#341) this file
    exists to catch.
    """
    alternatives = list(_BUILTIN_CONSTRUCTIONS)
    alternatives += [r'\bnew\s+' + name + r'\s*\(' for name in
                     _layer_classes(page)]
    return '|'.join(alternatives)


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
    assert _layer_classes(page), (
        'no L.TileLayer subclass found -- the construction pattern would '
        'then guard only the built-in constructors')
    sites = [m.start() for m in re.finditer(_constructions(page), page)]
    assert len(sites) >= 4, (
        'expected at least imagery, bathymetry, hillshade and coverage; '
        'found {} -- has the page been restructured?'.format(len(sites)))
    for start in sites:
        statement = _statement(page, start)
        assert '.addTo(map)' in statement, (
            'the tile layer constructed at offset {} is never added to the '
            'map -- it will not appear, silently'.format(start))


# Leaflet's vector layers. These are how the boat itself is drawn -- the
# trail and the hull -- and they are NOT L.TileLayer constructions, so
# `test_every_layer_reaches_the_map` above does not see them. Orphaning one
# leaves the vessel off the map entirely with the suite green: the same #341
# failure class, on the layers that matter most to an operator.
_VECTOR_CONSTRUCTIONS = (
    r'\bL\.polyline\s*\(',
    r'\bL\.polygon\s*\(',
    r'\bL\.circleMarker\s*\(',
    r'\bL\.marker\s*\(',
)

_VECTOR_BINDING = r'(?:const|let|var)\s+(\w+)\s*=\s*$'


def test_every_vector_layer_reaches_the_map():
    """Every vector-layer construction must reach the map.

    Either in its own statement, or later through the name it is bound to --
    a layer built now and added on a state change is legitimate, so the name
    is followed rather than requiring addTo at the construction site.
    """
    page = _code(_page())
    sites = [m.start() for m in
             re.finditer('|'.join(_VECTOR_CONSTRUCTIONS), page)]
    assert len(sites) >= 2, (
        'expected at least the trail and the hull; found {} -- has the page '
        'been restructured?'.format(len(sites)))
    for start in sites:
        statement = _statement(page, start)
        if '.addTo(map)' in statement:
            continue
        bound = re.search(_VECTOR_BINDING, page[:start])
        assert bound, (
            'the vector layer constructed at offset {} is neither added to '
            'the map nor bound to a name -- it cannot appear'.format(start))
        name = bound.group(1)
        assert re.search(r'\b' + name + r'\s*\.addTo\s*\(\s*map\s*\)', page), (
            '{} is constructed but never added to the map -- it will not '
            'appear, silently'.format(name))


def test_the_coverage_layer_is_configured_from_the_manifest():
    """The page must not hardcode the zoom the renderer writes.

    The two drifted apart once already: a layer pinned to the wrong native
    zoom requests tiles that were never written, one 403 per tile per pan.

    The zoom now reaches `buildCoverage` through `saneZoom(meta.zoom)` rather
    than raw, so this pins that chain end to end: the manifest field is read,
    it is the argument the validator is given, and the validated result is
    what builds the layer.
    """
    page = _page()
    assert 'meta.json' in page, 'the coverage manifest is not fetched'
    assert re.search(r'saneZoom\(\s*meta\.zoom\s*\)', page), (
        'the coverage zoom no longer comes from the manifest')
    assert re.search(r'buildCoverage\(\s*zoom\s*\)', page), (
        'the coverage layer is not built from the validated manifest zoom')
    assert not re.search(r'COVERAGE_Z\s*=\s*\d', page), (
        'the render zoom is hardcoded in the page again')


def test_the_manifest_is_validated_before_it_configures_the_layer():
    """meta.json is remote input, and both fields it drives fail badly.

    `typeof x === 'number'` admits NaN and Infinity. `zoom` sets minZoom and
    minNativeZoom -- the bound that keeps Leaflet from laying the viewport out
    in millions of native-zoom tiles -- and `stamp` drives the liveness age,
    where NaN makes every staleness comparison false and the panel reports a
    healthy tile count for a dead renderer.
    """
    page = _code(_page())
    assert re.search(r'Number\.isInteger\(\s*value\s*\)', page), (
        'the manifest zoom is not checked for integrality; NaN, Infinity and '
        '15.5 all satisfy a bare typeof check')
    assert re.search(r'value\s*<=\s*COVERAGE_MAX_Z', page), (
        'the manifest zoom is not bounded against the layer maxZoom')
    assert 'Number.isFinite(value)' in page, (
        'the manifest stamp is not checked for finiteness; a missing stamp '
        'makes the age NaN and the renderer reads as alive forever')
    assert re.search(r'zoom\s*===\s*null\s*\|\|\s*stamp\s*===\s*null',
                     page), (
        'a manifest that fails validation is still used to configure the '
        'layer')


def test_a_stale_manifest_degrades_the_coverage_layer():
    """A dead renderer must not present as a confident mosaic.

    Every miss is painted transparent, so whatever the renderer last managed
    to upload keeps rendering at full opacity indefinitely once it dies. The
    readout alone is not enough -- the map is what people look at.
    """
    page = _code(_page())
    assert 'COVERAGE_STALE_OPACITY' in page, (
        'the coverage layer has no degraded opacity for a dead renderer')
    assert re.search(r'setCoverageAlive\(\s*age\s*<=\s*COVERAGE_DEAD_S\s*\)',
                     page), (
        'the layer opacity is not driven by the manifest age')
    assert re.search(r'catch[^{]*\{[^}]*setCoverageAlive\(\s*false\s*\)',
                     page, re.S), (
        'an unreachable or malformed manifest leaves the layer at full '
        'opacity')


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


def test_the_construction_pattern_tracks_the_pages_layer_classes():
    """A new layer subclass must be guarded without editing this file.

    The alternation used to spell out `Bathy|Relief`. A fourth subclass added
    to the page would have been constructed, orphaned and never noticed --
    exactly the #341 failure the file exists to catch.
    """
    page = _code(_page())
    pattern = _constructions(page)
    for name in _layer_classes(page):
        assert re.search(pattern, 'x = new {}({{}});'.format(name)), (
            '{} is not covered by the construction pattern'.format(name))

    invented = 'const Sidescan = L.TileLayer.extend({});'
    assert 'Sidescan' in _layer_classes(invented), (
        'a newly defined layer class is not discovered')
    assert re.search(_constructions(invented), 'x = new Sidescan({});'), (
        'a newly defined layer class would escape the addTo check')
