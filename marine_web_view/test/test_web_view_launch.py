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


"""Guard the wiring `web_view_launch.py` does on behalf of three renderers.

This file brings up `state_renderer`, `coverage_renderer` and `ais_renderer`
together, and every failure it can introduce is a SILENT one: the launch
succeeds, three nodes come up, and the artifacts are wrong. Nothing raises,
so nothing but a test will say so.

Unlike `test_launch_params.py`, which reads the launch files as text, these
tests EVALUATE the launch description -- visiting the includes and resolving
the substitutions -- because the failures below live in what the arguments
resolve TO inside each include, which no amount of grepping can see. The
launch-time crash fixed in `d3b1642` (`ParameterValue` rejecting a bare
`list` as `value_type`) was this same class: invisible to static reading,
immediate on evaluation.
"""

import importlib.util
import os

from launch import LaunchContext
from launch.substitutions import TextSubstitution
from launch.utilities import normalize_to_list_of_substitutions
from launch.utilities import perform_substitutions

from launch_ros.actions import Node

PACKAGE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The two renderers that own an S3 key, in the order web_view_launch.py
# includes them, with the key and interval each must end up with.
# coverage_renderer is deliberately absent: it declares neither argument,
# because it publishes a tile tree rather than one object. The uniqueness
# check below still covers all three. Sharing a key is the failure this file
# exists for, so the expected values are written out rather than derived.
EXPECTED = (
    ('state_renderer', 'live/position.geojson', '1.0'),
    ('ais_renderer', 'live/ais.geojson', '10.0'),
)


def _load_launch_module():
    """Import web_view_launch.py from the source tree under test.

    `FindPackageShare` is replaced with the source package root. Left alone
    it resolves through the ament index to the INSTALLED share directory,
    which is a different copy of these launch files -- so the test would pass
    or fail on whatever happens to be built rather than on the tree it is
    checking. Substituting the root keeps it hermetic.
    """
    path = os.path.join(PACKAGE_ROOT, 'launch', 'web_view_launch.py')
    spec = importlib.util.spec_from_file_location('web_view_launch', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.FindPackageShare = lambda package: TextSubstitution(
        text=PACKAGE_ROOT)
    return module


def _resolve(**overrides):
    """Evaluate the launch description; return one config dict per node.

    Returns the launch configurations visible AT each `Node`, in include
    order, which is what the node actually receives. Reading them at the top
    level instead would miss the whole point: the scoping this file checks is
    precisely what makes those two different.
    """
    module = _load_launch_module()
    context = LaunchContext()
    for name, value in overrides.items():
        context.launch_configurations[name] = value
    resolved = []

    def walk(entities):
        for entity in entities:
            if isinstance(entity, Node):
                resolved.append(dict(context.launch_configurations))
                continue
            sub_entities = entity.visit(context)
            if sub_entities:
                walk(sub_entities)

    walk(module.generate_launch_description().entities)
    return resolved


def test_each_renderer_keeps_its_own_destination():
    """The scoping guard, and the most expensive failure in the file.

    The three included files declare overlapping argument names -- `key`,
    `interval`, `local_path` -- with per-renderer defaults, and
    `DeclareLaunchArgument` does not overwrite a configuration already set.
    Without `scoped=True` on each include, the first one to declare a name
    wins it for every later sibling, and `ais_renderer` inherits
    `state_renderer`'s key: it PUTs its contact collection over
    `live/position.geojson` and takes the boat off its own map, at
    state_renderer's 1 s interval rather than its own 10 s.

    Nothing about that is visible at launch. The nodes start, the PUTs
    succeed, and the page simply stops showing the vessel.
    """
    resolved = _resolve()
    assert len(resolved) == 3, (
        'expected three renderers, resolved {} -- if a renderer was added or '
        'removed, update EXPECTED rather than loosening this'
        .format(len(resolved)))

    keys = [config.get('key') for config in resolved if config.get('key')]
    assert len(set(keys)) == len(keys), (
        'two renderers resolved to the same S3 key ({}) -- one is '
        'overwriting the other. Check that every include in '
        'web_view_launch.py is still scoped=True'.format(sorted(keys)))

    by_key = {config.get('key'): config for config in resolved}
    for renderer, key, interval in EXPECTED:
        assert key in by_key, (
            '{} did not resolve to its own key {} -- got {}'
            .format(renderer, key, sorted(by_key)))
        assert by_key[key].get('interval') == interval, (
            '{} resolved to interval {} rather than its own {} -- a sibling '
            "include's default has leaked into it"
            .format(renderer, by_key[key].get('interval'), interval))


def test_every_argument_passed_to_an_include_is_declared_there():
    """An argument the include does not declare is silently dropped.

    `IncludeLaunchDescription` sets an unrecognised argument as a plain
    launch configuration rather than raising -- its own documentation says
    so, because it cannot reliably detect every `DeclareLaunchArgument` in
    the included description. So a renamed or mistyped argument here does not
    fail: the node keeps its default, the override does nothing, and the
    result is the drift `test_launch_params.py` was written for after #341.

    That file checks each launch file against its own node. This checks the
    direction it cannot see -- across the include boundary.
    """
    module = _load_launch_module()
    context = LaunchContext()
    checked = 0

    def walk(entities):
        nonlocal checked
        for entity in entities:
            if isinstance(entity, Node):
                continue
            # Read the arguments BEFORE visiting and the source location
            # after: the location is a substitution until the include
            # resolves it.
            passed = getattr(entity, 'launch_arguments', None)
            sub_entities = entity.visit(context)
            if passed:
                source = entity.launch_description_source
                declared = {
                    argument.name for argument
                    in source.get_launch_description(context)
                    .get_launch_arguments()}
                names = {
                    perform_substitutions(
                        context, normalize_to_list_of_substitutions(name))
                    for name, _ in passed}
                unknown = names - declared
                assert not unknown, (
                    'web_view_launch.py passes {} to {}, which does not '
                    'declare it -- the value is silently ignored and the '
                    'renderer keeps its own default'
                    .format(sorted(unknown), os.path.basename(source.location)))
                checked += 1
            if sub_entities:
                walk(sub_entities)

    walk(module.generate_launch_description().entities)
    assert checked == 3, (
        'expected to check three includes, saw {} -- this guard would pass '
        'vacuously; update it rather than removing it'.format(checked))


def test_the_ais_contacts_topic_is_not_derived_from_the_platform():
    """The field regression fixed in `22046b7`, pinned.

    Derived from `platform`, `contacts_topic` became `/bizzy/ais/contacts`.
    That topic is real, but boat-side -- it is what the bags carry, because
    BizzyBoat has its own receiver. This file is for the OPERATOR STATION,
    which runs its own nmea_relay -> ais_parser -> ais_contact_tracker chain
    on the global `/ais/contacts`.

    Subscribed to a topic that does not exist there, the renderer published
    an empty collection forever, and the page read as a quiet river rather
    than as a misconfiguration. Deriving it again would restore exactly that.
    """
    resolved = _resolve()
    topics = {config.get('contacts_topic') for config in resolved}
    assert '/ais/contacts' in topics, (
        'the AIS renderer no longer defaults to the station receiver on '
        '/ais/contacts -- got {}'.format(sorted(t for t in topics if t)))
    for platform in ('bizzy', 'izzy', 'ben'):
        resolved = _resolve(platform=platform)
        topics = {config.get('contacts_topic') for config in resolved}
        assert '/{}/ais/contacts'.format(platform) not in topics, (
            'contacts_topic is derived from platform again; the operator '
            'station does not carry /{}/ais/contacts and the renderer would '
            'publish an empty collection forever'.format(platform))


def test_platform_name_follows_the_platform():
    """`platform_name` must track `platform`, or the hull is silently wrong.

    `state_renderer` adopts a platform only when the name matches
    `PlatformList.platform_namespace` or `.name`. Unmatched, it subscribes to
    nothing and keeps the fallback hull -- BEN's 4.25 x 1.70 m over
    BizzyBoat's 2.40 x 0.90 m. The page then draws a hull nearly twice its
    true size over a boat that never moves, and nothing is logged as wrong.
    """
    for platform in ('bizzy', 'izzy', 'ben'):
        resolved = _resolve(platform=platform)
        names = {config.get('platform_name') for config in resolved}
        assert platform in names, (
            'platform:={} did not reach platform_name (got {}) -- '
            'state_renderer would keep the fallback hull'
            .format(platform, sorted(n for n in names if n)))


def test_each_renderer_can_be_disabled_on_its_own():
    """The enable gates must gate, and gate only their own renderer.

    `enable_coverage:=false` is the honest setting whenever the boat is not
    up -- the ADR-0008 tile protocol is request/response and is in no bag, so
    coverage renders nothing off-boat. A gate that silently did nothing would
    leave a node polling for a producer that cannot answer.
    """
    assert len(_resolve()) == 3
    for argument in ('enable_state', 'enable_coverage', 'enable_ais'):
        resolved = _resolve(**{argument: 'false'})
        assert len(resolved) == 2, (
            '{}:=false left {} renderers running rather than 2'
            .format(argument, len(resolved)))
