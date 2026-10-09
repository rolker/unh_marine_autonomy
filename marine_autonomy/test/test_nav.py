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

"""Tests for the TF-failure paths in marine_autonomy.nav.

A TF lookup that is not ready yet (the boat's frame does not exist, or the
transform listener has not received anything) must log and return None. It
must not raise: a handler that logs the exception object itself used to turn
the lookup failure into a TypeError in the caller (#408).
"""

import pytest
import rclpy
import rclpy.node
from geometry_msgs.msg import PointStamped
from nav_msgs.msg import Odometry
from tf2_ros.buffer import Buffer

from marine_autonomy.nav import EarthTransforms, RobotNavigation


class _RecordingLogger:
    """Delegates to the real rclpy logger and records what was logged.

    Delegating keeps the real argument checking, so a non-string message
    still raises TypeError as it does in production.
    """

    def __init__(self, real, sink):
        self._real = real
        self._sink = sink

    def __getattr__(self, name):
        real_method = getattr(self._real, name)

        def call(msg, *args, **kwargs):
            result = real_method(msg, *args, **kwargs)
            self._sink.append((name, msg))
            return result

        return call


@pytest.fixture(scope='module', autouse=True)
def ros_context():
    rclpy.init()
    yield
    rclpy.shutdown()


@pytest.fixture
def node():
    node = rclpy.node.Node('test_nav_tf_failures')
    logged = []
    real_get_logger = node.get_logger
    node.get_logger = lambda: _RecordingLogger(real_get_logger(), logged)
    node.logged = logged
    yield node
    node.destroy_node()


def _errors(node):
    return [msg for level, msg in node.logged if level == 'error']


def _assert_logged_strings(node, expected_prefix):
    errors = _errors(node)
    assert errors, 'expected the failure to be logged at error level'
    assert all(isinstance(m, str) for m in errors)
    assert errors[0].startswith(expected_prefix)
    # The exception text must follow the summary line, not be dropped.
    assert len(errors) >= 2 and errors[1]


def test_map_to_earth_transform_without_tf_returns_none(node):
    nav = EarthTransforms(node, Buffer(), map_frame='map')
    assert nav.mapToEarthTransform() is None
    _assert_logged_strings(node, 'Cannot lookup transform from <earth> to map')


def test_point_to_geo_point_without_tf_returns_none(node):
    nav = EarthTransforms(node, Buffer(), map_frame='map')
    point = PointStamped()
    point.header.frame_id = 'map'
    assert nav.pointToGeoPoint(point) is None
    _assert_logged_strings(node, 'Cannot lookup transform from <earth> to map')


def test_position_lat_lon_without_tf_returns_none(node):
    nav = RobotNavigation(node, Buffer())
    odom = Odometry()
    odom.header.frame_id = 'odom'
    nav.odometryCallback(odom)
    assert nav.positionLatLon() is None
    _assert_logged_strings(
        node, 'Cannot lookup transform from <earth> to odometry frame_id')


def test_position_lat_lon_without_odometry_returns_none(node):
    nav = RobotNavigation(node, Buffer())
    assert nav.positionLatLon() is None


def test_geo_to_pose_without_tf_returns_none(node):
    nav = EarthTransforms(node, Buffer(), map_frame='map')
    assert nav.geoToPose(43.0, -70.7) is None
    _assert_logged_strings(node, 'Cannot lookup transform from <earth> to map')
