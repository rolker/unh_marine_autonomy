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


def _function_span(page, name):
    """Return the (start, end) offsets of `function <name>()`'s body.

    Call it on `_code(page)`. A behaviour is only bound if it is checked in the
    function that actually runs it -- a `redraw()` that exists somewhere on the
    page but is never reached from the poll is exactly the defect this file
    keeps finding.
    """
    match = re.search(r'function\s+' + name + r'\s*\([^)]*\)\s*\{', page)
    assert match, 'the page has no function {}()'.format(name)
    depth = 0
    index = match.end() - 1
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
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return match.end(), index
        index += 1
    raise AssertionError('function {}() is never closed'.format(name))


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


def _option_value(page, at):
    """Return the value of the Leaflet option whose key starts at `at`.

    Paren-aware: `minZoom: Math.max(0, zoom - 2)` has a comma inside its own
    call, so stopping at the first comma reads the value as `Math.max(0` and
    any assertion made on it is meaningless.
    """
    index = page.index(':', at) + 1
    depth = 0
    start = index
    end = len(page)
    while index < end:
        char = page[index]
        if char in '([{':
            depth += 1
        elif char in ')]}':
            if depth == 0:
                break
            depth -= 1
        elif char == ',' and depth == 0:
            break
        index += 1
    return page[start:index].strip()


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


def test_the_refresh_does_not_redraw_a_hidden_layer():
    """`redraw()` must be gated on the layer actually being displayed.

    Leaflet applies a layer's `minZoom` only in `_setView`. `redraw()` goes
    through `_clampZoom`, which consults `min/maxNativeZoom` alone -- so
    redrawing a layer that `minZoom` is hiding un-hides it and lays the whole
    viewport out at the native zoom. Against the page's own map `minZoom` 8
    and a native 15 that is hundreds of thousands of DOM elements: the browser
    freeze the coverage layer's `minZoom` exists to prevent, reintroduced
    through the back door by the refresh added for stationary viewers.

    The guard must come BEFORE the redraw, so this pins the order rather than
    the mere presence of a zoom check somewhere in the function.
    """
    page = _code(_page())
    start = page.find('function refreshCoverage')
    assert start != -1, 'refreshCoverage() is gone -- has the page been restructured?'
    body = _statement(page, page.index('{', start))
    redraw = body.find('.redraw(')
    assert redraw != -1, 'refreshCoverage() no longer redraws -- the layer will not refresh'
    guard = body.find('getZoom()')
    assert guard != -1, (
        'refreshCoverage() does not consult the map zoom -- redraw() ignores '
        'minZoom and will lay the viewport out at the native zoom')
    assert 'minZoom' in body, (
        'refreshCoverage() does not compare against the layer minZoom')
    assert guard < redraw, (
        'the zoom guard runs after redraw() -- it cannot prevent the '
        'tile explosion it exists to prevent')


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
    # The CALL sites, not the definition: `buildCoverage\(\s*zoom\s*\)` also
    # matches `function buildCoverage(zoom)`, so bypassing the validator
    # entirely -- `buildCoverage(meta.zoom)` -- left this test green.
    code = _code(page)
    definition = re.search(r'function\s+buildCoverage\s*\(', code)
    assert definition, 'the page has no buildCoverage()'
    # The offset of the NAME inside the definition, not of `function`. The
    # first version of this exclusion compared against `definition.start()`
    # -- nine characters earlier -- so it excluded nothing, the definition
    # itself satisfied `assert calls`, and deleting the ONLY call to
    # buildCoverage left the whole suite green with no coverage layer ever
    # built. The test passed solely because the definition's parameter happens
    # to be spelled `zoom`.
    defined_at = definition.start() + definition.group(0).index('buildCoverage')
    calls = [m for m in re.finditer(r'\bbuildCoverage\s*\(\s*([^)]*?)\s*\)',
                                    code)
             if m.start() != defined_at]
    assert calls, (
        'buildCoverage() is defined but never called: the coverage layer is '
        'never built and the map shows no coverage at all')
    start, end = _function_span(code, 'pollCoverage')
    assert any(start <= call.start() < end for call in calls), (
        'buildCoverage() is never called from pollCoverage(): the layer is '
        'not built from the manifest the poll just read')
    for call in calls:
        assert call.group(1) == 'zoom', (
            'buildCoverage is called with {!r}: the layer must be built from '
            'the validated zoom, not from the raw manifest field'
            .format(call.group(1)))
    assert re.search(r'(?:const|let|var)\s+zoom\s*=\s*saneZoom\('
                     r'\s*meta\.zoom\s*\)', code), (
        'the name passed to buildCoverage is not the validated manifest zoom')
    assert not re.search(r'COVERAGE_Z\s*=\s*\d', page), (
        'the render zoom is hardcoded in the page again')


def test_the_coverage_layer_refreshes_for_a_stationary_viewer():
    """A viewer who does not touch the map must still see new coverage.

    Leaflet requests a tile only when it CREATES the element, and
    `errorTileUrl` makes a miss a permanently transparent tile that is never
    retried. Without an explicit `redraw()` on every poll the mosaic freezes at
    page load for anyone sitting still -- which is what watching a survey line
    run looks like -- and it shipped that way. The position and track layers
    re-render each poll; this pins that the tile layer does too.
    """
    page = _code(_page())
    sites = list(re.finditer(r'coverageLayer\s*\.redraw\s*\(\s*\)', page))
    assert sites, (
        'nothing on the page ever calls coverageLayer.redraw(): ground '
        'surveyed after page load will never appear for a stationary viewer')
    start, end = _function_span(page, 'pollCoverage')
    body = page[start:end]
    if any(start <= site.start() < end for site in sites):
        return
    # Reached through a helper: the helper must be called from the poll.
    holders = [name for name in re.findall(r'function\s+(\w+)\s*\(', page)
               if any(_function_span(page, name)[0] <= site.start()
                      < _function_span(page, name)[1] for site in sites)]
    assert holders, (
        'coverageLayer.redraw() is not inside any function -- it cannot run '
        'on the poll')
    assert any(re.search(r'\b' + name + r'\s*\(', body) for name in holders), (
        'coverageLayer.redraw() lives in {} but nothing in pollCoverage() '
        'calls it, so the tile layer is never refreshed'.format(holders))


def test_the_coverage_refresh_is_gated_on_the_change_signal():
    """The refresh must fire on "the tiles changed", not "a pass happened".

    `meta.json` is rewritten every render pass, idle or not, so gating the
    refresh on a new `stamp` means every viewer tears down and re-requests
    every tile under the viewport once per `render_interval` in perpetuity --
    with the sonar off and the boat docked. Most of that viewport is uncovered
    water, which answers 4xx with no cache headers at all, so nothing absorbs
    it: it is a per-viewer bill with no upper bound on a public page.

    This binds the guard EXPRESSION, not the presence of a token. Four
    mutations of the previous guard restored the defect with the suite green,
    so each assertion here names the mutation it catches.
    """
    page = _code(_page())
    start, end = _function_span(page, 'pollCoverage')
    body = page[start:end]

    gate = re.search(r'if\s*\(([^)]*)\)\s*refreshCoverage\s*\(\s*\)', body)
    assert gate, (
        'pollCoverage() does not call refreshCoverage() under a guard: the '
        'tile layer either never refreshes or refreshes unconditionally')
    terms = [term.replace(' ', '') for term in gate.group(1).split('&&')]
    assert len(terms) == 2, (
        'the refresh guard is {!r}: expected exactly the "not just rebuilt" '
        'and "tiles changed" terms'.format(gate.group(1)))
    assert terms[0] == '!rebuilt', (
        'the refresh guard reads {!r}: dropping the negation makes it refresh '
        'ONLY on a rebuild, i.e. never again after the first poll'
        .format(gate.group(1)))
    flag = terms[1]
    assert re.fullmatch(r'\w+', flag), (
        'the second guard term is {!r}, not a name this test can follow to '
        'its definition'.format(flag))

    defined = re.search(r'(?:const|let|var)\s+' + flag + r'\s*=\s*([^;]+);',
                        body)
    assert defined, (
        '{} is not defined in pollCoverage(): the guard cannot be checked '
        'against the manifest'.format(flag))
    expression = defined.group(1).strip()

    validated = re.search(r'(?:const|let|var)\s+(\w+)\s*=\s*saneCount\('
                          r'\s*meta\.rendered_tiles\s*\)', body)
    assert validated, (
        'the change signal is not read from meta.rendered_tiles through '
        'saneCount(): an unvalidated NaN compares unequal to itself and '
        'refreshes every tile on every single poll')
    signal = validated.group(1)

    assert signal in expression, (
        '{} = {!r} does not use the validated change signal {}'
        .format(flag, expression, signal))
    assert 'coverageRendered' in expression, (
        '{} = {!r} is not a comparison against the counter remembered from '
        'the last manifest -- a constant here either refreshes forever (a '
        'dead renderer redrawing every poll, unbounded) or never'
        .format(flag, expression))
    assert 'stamp' not in expression, (
        '{} = {!r} is gated on the manifest stamp again: the stamp moves on '
        'every pass, idle or not'.format(flag, expression))
    assert '!== null' in expression or '!=null' in expression.replace(' ', ''), (
        '{} = {!r} does not exclude a null change signal, so a manifest '
        'without the field fires one spurious full refresh'
        .format(flag, expression))
    assert re.search(r'coverageRendered\s*=\s*' + signal + r'\s*;', body), (
        'the remembered change signal is never updated, so every poll after '
        'the first sees a change and refreshes every tile')


def test_the_manifest_poll_bypasses_the_browser_cache():
    """The liveness manifest must never be read out of the HTTP cache.

    The renderer stamps `meta.json` with a `max-age` and the page polls it on
    roughly the same period, so with the cache in play a poll is routinely
    answered from the browser's copy of the PREVIOUS pass. Both of the things
    this manifest exists for then fail silently and identically to success:
    `rendered_tiles` does not move, so the change gate never fires and ground
    surveyed since page load never appears for a stationary viewer; and
    `stamp` is the old one, so the age is under-reported and a renderer that
    has died still reads as alive.

    It must not be done with a cache-busting query string either: a URL that
    changes per request also defeats the CloudFront edge cache, turning one
    origin fetch per interval into one per viewer per interval.
    """
    page = _code(_page())
    start, end = _function_span(page, 'pollCoverage')
    body = page[start:end]
    fetch = re.search(r'fetch\s*\(\s*COVERAGE_META\s*([^)]*)\)', body)
    assert fetch, (
        'pollCoverage() does not fetch COVERAGE_META -- has the page been '
        'restructured?')
    options = fetch.group(1).replace(' ', '')
    assert "cache:'no-store'" in options or 'cache:"no-store"' in options, (
        'the manifest is fetched with {!r}: anything the HTTP cache may '
        'answer means a poll can be served the previous pass, which silently '
        'defeats both the change signal and the dead-renderer detection'
        .format(fetch.group(0)))
    raw = _page()
    raw_fetch = re.search(r'fetch\s*\(\s*COVERAGE_META[^)]*\)', raw)
    assert raw_fetch and '+' not in raw_fetch.group(0), (
        'the manifest URL is built per request ({}): a cache-buster defeats '
        'the CDN edge cache for every viewer'.format(raw_fetch.group(0)))
    assert 'COVERAGE_META = ' in raw and '?' not in re.search(
        r'COVERAGE_META\s*=\s*([^;]+);', raw).group(1), (
        'COVERAGE_META carries a query string -- see above')


def test_the_coverage_layer_minzoom_is_clamped():
    """A negative `minZoom` disables the hide-when-zoomed-out rule entirely.

    `minZoom` is the only thing that keeps Leaflet from laying the viewport
    out at the layer's single native zoom, and the page also gates
    `refreshCoverage()` on `map.getZoom() < options.minZoom`. The manifest
    zoom is remote input: at zoom 0 or 1 an unclamped `zoom - 2` is negative,
    which is below every zoom the map can reach, so the layer is never hidden
    and that gate can never fire. (This is not the tile-explosion direction --
    a low native zoom means fewer tiles -- but the rule has to hold at every
    zoom a manifest may legally carry, not only the ones rendered today.)
    """
    page = _code(_page())
    start, end = _function_span(page, 'buildCoverage')
    body = page[start:end]
    at = body.find('minZoom')
    assert at != -1, 'buildCoverage() no longer sets a minZoom on the layer'
    expression = _option_value(body, at).replace(' ', '')
    clamp = re.fullmatch(r'Math\.max\(0,zoom-(\w+)\)', expression)
    assert clamp, (
        'the layer minZoom is {!r}: it must be clamped at 0 as '
        'Math.max(0, zoom - <margin>), because an unclamped subtraction goes '
        'negative on a low manifest zoom and the layer is then never hidden'
        .format(expression))
    margin = clamp.group(1)
    if not margin.isdigit():
        defined = re.search(r'(?:const|let|var)\s+' + margin +
                            r'\s*=\s*(-?\d+)', page)
        assert defined, (
            'the minZoom margin {} is not a literal this test can '
            'resolve'.format(margin))
        margin = defined.group(1)
    assert int(margin) >= 0, (
        'the minZoom margin is {}: a negative margin puts minZoom ABOVE the '
        'rendered zoom, hiding the layer where its own tiles exist'
        .format(margin))


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
