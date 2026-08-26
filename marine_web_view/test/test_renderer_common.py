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

"""Bind the two helpers the renderers share.

They were extracted from `state_renderer` when `ais_renderer` needed the same
conversion and the same write. Extraction removes the duplication but not the
risk: nothing in either renderer's own tests pins the DIRECTION of the compass
conversion (a sign flip renders a plausible heading, just the wrong one), and
the atomic write's whole point -- that a viewer never sees a partial file --
is invisible in a passing run.
"""

import math
import os

from marine_web_view.renderer_common import compass_degrees
from marine_web_view.renderer_common import heading_from_quaternion
from marine_web_view.renderer_common import write_atomic
from marine_web_view.renderer_common import yaw_from_quaternion


class _Quaternion:
    """A geometry_msgs/Quaternion-shaped value, without the ROS dependency."""

    def __init__(self, yaw_radians):
        self.x = 0.0
        self.y = 0.0
        self.z = math.sin(yaw_radians / 2.0)
        self.w = math.cos(yaw_radians / 2.0)


def test_the_compass_conversion_runs_the_right_way_round():
    """ENU yaw is counter-clockwise from EAST; a heading is clockwise from N.

    A sign error here is invisible in every other test: the page still draws
    a hull, at a heading that is simply wrong. These four are the cardinal
    directions, which is where a flipped sign is unambiguous -- north and
    south survive a flip, east and west do not.
    """
    assert compass_degrees(0.0) == 90.0                       # east
    assert compass_degrees(math.radians(90.0)) == 0.0         # north
    assert compass_degrees(math.radians(180.0)) == 270.0      # west
    assert compass_degrees(math.radians(-90.0)) == 180.0      # south


def test_the_conversion_wraps_into_zero_to_360():
    """A heading of -1 degrees is 359, not a negative number the page shows."""
    assert compass_degrees(math.radians(100.0)) == 350.0
    assert 0.0 <= compass_degrees(math.radians(1000.0)) < 360.0


def test_a_quaternion_yields_the_same_heading():
    """The quaternion path must agree with the yaw path it wraps."""
    for degrees in (0.0, 37.0, 90.0, 180.0, -125.0):
        yaw = math.radians(degrees)
        quaternion = _Quaternion(yaw)
        assert math.isclose(yaw_from_quaternion(quaternion), yaw,
                            abs_tol=1e-9)
        assert math.isclose(heading_from_quaternion(quaternion),
                            compass_degrees(yaw), abs_tol=1e-9)


def test_no_quaternion_is_no_heading():
    """`None` in, `None` out -- the JSON null the page reads as unknown."""
    assert heading_from_quaternion(None) is None


def test_the_local_write_leaves_no_temp_file_behind(tmp_path):
    """The preview directory is served as-is, so a leftover temp is served."""
    target = str(tmp_path / 'position.geojson')
    write_atomic(target, '{"first": true}')
    assert os.listdir(str(tmp_path)) == ['position.geojson']
    write_atomic(target, '{"second": true}')
    assert os.listdir(str(tmp_path)) == ['position.geojson']
    with open(target) as handle:
        assert handle.read() == '{"second": true}'


def test_the_temp_file_is_a_sibling_of_the_target(tmp_path):
    """os.replace is atomic only within one filesystem.

    A temp file written to /tmp and renamed onto a target elsewhere is a
    cross-device rename: it either fails outright or degrades to a copy that
    a viewer can read half of. Pinning the sibling directory is what keeps
    the rename on one filesystem.
    """
    directory = tmp_path / 'live'
    directory.mkdir()
    target = str(directory / 'ais.geojson')
    seen = []

    real_open = open

    def _watching_open(path, *args, **kwargs):
        seen.append(path)
        return real_open(path, *args, **kwargs)

    import builtins
    builtins.open = _watching_open
    try:
        write_atomic(target, '[]')
    finally:
        builtins.open = real_open
    assert seen, 'write_atomic no longer opens a file'
    assert os.path.dirname(seen[0]) == str(directory), (
        'the temp file {} is not a sibling of {}'.format(seen[0], target))
