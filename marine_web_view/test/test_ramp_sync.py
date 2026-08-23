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

"""Guard that the depth-colour ramp is identical in Python and JS.

RAMP / MAX_DEPTH / STEP are duplicated in scripts/refresh_chart_tiles.py and
web/index.html because a browser page and a Python CLI share no module. If they
drift, pre-rendered tiles and the live-render fallback stop matching -- a
visible seam that is permanent once cached. This turns the "keep in sync"
comment into an enforced check.
"""

import importlib.util
import os
import re

_TEST_DIR = os.path.dirname(__file__)
_PKG_DIR = os.path.dirname(_TEST_DIR)
_SCRIPT = os.path.join(_PKG_DIR, 'scripts', 'refresh_chart_tiles.py')
_INDEX = os.path.join(_PKG_DIR, 'web', 'index.html')


def _load_script():
    """Import refresh_chart_tiles.py by path (stdlib-only, safe to import)."""
    spec = importlib.util.spec_from_file_location('refresh_chart_tiles', _SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _js_triples(name, text):
    """Return the RGB triples of a ``const <name> = [ ... ];`` JS array."""
    body = re.search(r'const\s+' + name + r'\s*=\s*\[(.*?)\];', text, re.S)
    assert body, 'could not find const {} in index.html'.format(name)
    return [tuple(int(v) for v in row) for row in re.findall(
        r'\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]', body.group(1))]


def _js_number(name, text):
    """Return the value of a ``const <name> = <number>;`` JS constant."""
    match = re.search(r'const\s+' + name + r'\s*=\s*([0-9.]+)\s*;', text)
    assert match, 'could not find const {} in index.html'.format(name)
    return float(match.group(1))


def test_ramp_in_sync():
    """RAMP / MAX_DEPTH / STEP must match between the script and the page."""
    py = _load_script()
    with open(_INDEX, encoding='utf-8') as handle:
        html = handle.read()

    assert [tuple(row) for row in py.RAMP] == _js_triples('RAMP', html), \
        'RAMP differs between refresh_chart_tiles.py and index.html'
    assert float(py.MAX_DEPTH) == _js_number('MAX_DEPTH', html), \
        'MAX_DEPTH differs between refresh_chart_tiles.py and index.html'
    assert float(py.STEP) == _js_number('STEP', html), \
        'STEP differs between refresh_chart_tiles.py and index.html'


def test_coverage_node_shares_the_page_ramp():
    """The coverage node must use the SAME ramp as the page and tile script.

    A third copy was hand-transcribed into coverage_renderer.py and came out
    shifted -- 14 of 24 stops differed -- while three documents and the plan
    all asserted the shared-scale property that the code did not have. This
    guard covered only two copies at the time, so nothing caught it.
    """
    node = open(os.path.join(_PKG_DIR, 'marine_web_view',
                             'coverage_renderer.py')).read()
    page = open(_INDEX).read()

    page_ramp = _js_triples('RAMP', page)
    node_ramp = [tuple(int(v) for v in row) for row in re.findall(
        r'\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)',
        node[node.index('RAMP = ('):node.index('MAX_DEPTH = ')])]

    assert node_ramp == page_ramp, (
        'coverage_renderer RAMP differs from the page at {} of {} stops'
        .format(sum(1 for a, b in zip(page_ramp, node_ramp) if a != b),
                len(page_ramp)))

    node_max = float(re.search(r'MAX_DEPTH\s*=\s*([0-9.]+)',
                               node).group(1))
    assert node_max == _js_number('MAX_DEPTH', page), (
        'MAX_DEPTH differs between the coverage node and the page')
