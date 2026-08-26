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

"""Guard that every node parameter is reachable from its launch file.

A node can declare a parameter and its launch file can quietly not forward
it: nothing errors, the parameter silently keeps its default, and the feature
appears broken. That is exactly what happened in #341 -- the launch file
declared 8 of state_renderer's 20 parameters while the README documented
passing one of the missing ones, so the documented preview never rendered a
trail and nothing said why.

Checking once is not enough, because the drift reappears the moment a
parameter is added. This pins it.
"""

import os
import re

PACKAGE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Parameters every ROS node has without declaring them. A launch file may
# legitimately expose one, and there is no `declare_parameter` behind it to
# match against -- re-declaring `use_sim_time` raises rather than working.
ROS_BUILTIN_PARAMETERS = frozenset({'use_sim_time'})

# node module -> launch file that is expected to expose it
PAIRS = (
    ('state_renderer.py', 'state_renderer_launch.py'),
    ('coverage_renderer.py', 'coverage_renderer_launch.py'),
    ('ais_renderer.py', 'ais_renderer_launch.py'),
)


def _read(*parts):
    """Return the text of a file under the package root."""
    with open(os.path.join(PACKAGE_ROOT, *parts)) as handle:
        return handle.read()


def _node_parameters(module):
    """Return the parameter names a node declares."""
    return set(re.findall(r"declare_parameter\(\s*'([a-z_0-9]+)'",
                          _read('marine_web_view', module)))


def _launch_arguments(launch_file):
    """Return the launch arguments a launch file declares."""
    return set(re.findall(r"DeclareLaunchArgument\(\s*'([a-z_0-9]+)'",
                          _read('launch', launch_file)))


def _forwarded_parameters(launch_file):
    """Return the parameter names a launch file actually passes to the node.

    Two spellings are in use and both are hand-maintained, so both can drift
    from the DeclareLaunchArgument list independently:

    * an explicit ``{'name': LaunchConfiguration('name'), ...}`` dict
      (state_renderer), and
    * a ``names = (...)`` tuple fed to a comprehension (coverage_renderer).

    Checking only that an argument is *declared* misses the exact failure this
    file exists for: #341 declared arguments the node never received.
    """
    text = _read('launch', launch_file)
    forwarded = set(re.findall(
        r"'([a-z_0-9]+)'\s*:\s*LaunchConfiguration", text))
    if not forwarded:
        match = re.search(r'names\s*=\s*\(([^)]*)\)', text, re.S)
        assert match, (
            '{} forwards parameters in neither known form -- this guard '
            'cannot see them, so update it rather than removing it'
            .format(launch_file))
        forwarded = set(re.findall(r"'([a-z_0-9]+)'", match.group(1)))
    return forwarded


def _documented_parameters():
    """Return the parameter names the README's tables carry.

    Table rows only -- a name that appears in prose is discussion, not
    documentation of a parameter's default and meaning.
    """
    names = set()
    for line in _read('README.md').splitlines():
        if line.startswith('|'):
            names.update(re.findall(r'`([a-z_0-9]+)`', line))
    return names


def test_the_readme_documents_every_node_parameter():
    """The documentation leg of the #341 drift class.

    The launch leg is enforced above; the README was not, and drifted the same
    way: `state_renderer` shipped with 16 of its 20 parameters in the table
    while the Running section told you to pass one of the missing four. A
    parameter nobody can find is as good as one that does not work.
    """
    documented = _documented_parameters()
    assert len(documented) >= 10, (
        'no parameter tables found in the README -- this guard would pass '
        'vacuously; update it rather than removing it')
    for module, _ in PAIRS:
        missing = _node_parameters(module) - documented
        assert not missing, (
            '{} declares {} which no README parameter table documents'
            .format(module, sorted(missing)))


def test_launch_files_forward_every_argument_they_declare():
    """A declared-but-unforwarded argument is silently ignored."""
    for _, launch_file in PAIRS:
        exposed = _launch_arguments(launch_file)
        forwarded = _forwarded_parameters(launch_file)
        missing = exposed - forwarded
        assert not missing, (
            '{} declares {} but never passes them to the node -- setting '
            'them on the command line does nothing'
            .format(launch_file, sorted(missing)))
        extra = forwarded - exposed
        assert not extra, (
            '{} forwards {} which it does not declare'
            .format(launch_file, sorted(extra)))


def test_launch_files_expose_every_node_parameter():
    """Each launch file must declare an argument per node parameter."""
    for module, launch_file in PAIRS:
        declared = _node_parameters(module)
        exposed = _launch_arguments(launch_file)
        missing = declared - exposed
        assert not missing, (
            '{} declares {} but {} does not expose it -- passing it on the '
            'command line would be silently ignored'
            .format(module, sorted(missing), launch_file))


def test_launch_files_do_not_invent_parameters():
    """A launch argument with no matching node parameter is dead config."""
    for module, launch_file in PAIRS:
        declared = _node_parameters(module)
        exposed = _launch_arguments(launch_file)
        extra = exposed - declared - ROS_BUILTIN_PARAMETERS
        assert not extra, (
            '{} exposes {} which {} does not declare'
            .format(launch_file, sorted(extra), module))


def test_the_ais_renderer_can_be_run_against_a_bag():
    """The documented way to exercise the AIS layer needs the clock.

    `ais_renderer` measures every contact's age against its own clock while
    the ages come from the header stamps the bag recorded. Left on the wall
    clock, a replay hands it stamps as old as the recording, `contact_timeout`
    expires all of them on the first tick, and the artifact reads
    `contacts: 0` with nothing anywhere saying why. The launch file has to
    expose `use_sim_time` and the recipe has to play the bag with `--clock`;
    either half alone still fails.
    """
    launch_file = 'ais_renderer_launch.py'
    assert 'use_sim_time' in _launch_arguments(launch_file), (
        '{} does not expose use_sim_time, so a bag replay cannot be told to '
        'follow the recorded clock'.format(launch_file))
    assert 'use_sim_time' in _forwarded_parameters(launch_file), (
        '{} declares use_sim_time but never passes it to the node'
        .format(launch_file))
    readme = _read('README.md')
    assert 'ros2 bag play' in readme, 'the replay recipe is gone'
    recipe = readme[readme.index('ros2 bag play'):]
    recipe = recipe[:recipe.index('```')]
    assert '--clock' in recipe, (
        'the replay recipe plays the bag without --clock, so nothing ever '
        'publishes /clock and use_sim_time has no time to follow')


def test_nodes_declare_parameters_at_all():
    """Guard the guard: a regex that matches nothing would pass vacuously."""
    for module, _ in PAIRS:
        assert len(_node_parameters(module)) >= 5, module
