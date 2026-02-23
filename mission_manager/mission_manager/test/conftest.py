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

"""Shared test fixtures for mission_manager tests.

Mocks all ROS2 and external dependencies before any test module imports
mission_manager, so that tests can run without a ROS2 installation.
"""

import sys
from unittest.mock import MagicMock


class _FakeNode:
    """Stand-in for rclpy Node base classes.

    A raw MagicMock cannot be used as a base class because Python uses
    the mock's metaclass to create the subclass, making the subclass
    itself a MagicMock instance.  Calling such a "class" eventually
    exhausts an internal iterator and raises StopIteration.

    This real class lets production code (MissionManager,
    MultibeamCoverageAdapter) inherit normally.  Any attribute not set
    during __init__ is auto-created as a MagicMock via __getattr__.
    """

    def __init__(self, *args, **kwargs):
        """Accept and ignore all arguments."""
        pass

    def __getattr__(self, name):
        """Return a MagicMock for any attribute not in the instance dict."""
        mock = MagicMock()
        object.__setattr__(self, name, mock)
        return mock


# Comprehensive list of all external modules imported transitively by
# mission_manager/__init__.py -> camp_interface.py, mission_manager.py,
# multibeam_coverage_adapter.py
_MOCK_MODULES = [
    # rclpy
    'rclpy', 'rclpy.action', 'rclpy.action.client', 'rclpy.action.server',
    'rclpy.callback_groups', 'rclpy.executors', 'rclpy.lifecycle',
    'rclpy.node', 'rclpy.qos', 'rclpy.service', 'rclpy.subscription',
    'rclpy.task',
    # ROS2 message packages
    'geographic_msgs', 'geographic_msgs.msg',
    'geometry_msgs', 'geometry_msgs.msg',
    'marine_nav_interfaces', 'marine_nav_interfaces.action',
    'marine_nav_interfaces.msg',
    'mission_manager_interfaces', 'mission_manager_interfaces.srv',
    'nav_msgs', 'nav_msgs.msg',
    'marine_autonomy', 'marine_autonomy.nav',
    'marine_interfaces', 'marine_interfaces.msg',
    'std_msgs', 'std_msgs.msg',
    # Other external
    'marine_nav_tasks',
]

for _mod in _MOCK_MODULES:
    sys.modules.setdefault(_mod, MagicMock())

# Replace the Node attribute on mock modules with a real class so that
# production classes can inherit without MagicMock metaclass issues.
sys.modules['rclpy.lifecycle'].Node = _FakeNode
sys.modules['rclpy.node'].Node = _FakeNode

# Message constructors must return a fresh object on each call.  A plain
# MagicMock attribute always returns the same return_value, so two
# "instances" share state and attribute assignments clobber each other.
# Setting side_effect=MagicMock makes each call produce a distinct mock.
sys.modules['marine_nav_interfaces.msg'].TaskInformation.side_effect = (
    MagicMock)
sys.modules['marine_nav_interfaces.msg'].TaskFeedback.side_effect = (
    MagicMock)
sys.modules['marine_interfaces.msg'].BehaviorInformation.side_effect = (
    MagicMock)
