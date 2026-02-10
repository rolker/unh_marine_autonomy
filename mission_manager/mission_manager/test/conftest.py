# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause

"""Shared test fixtures for mission_manager tests.

Mocks all ROS2 and external dependencies before any test module imports
mission_manager, so that tests can run without a ROS2 installation.
"""

import sys
from unittest.mock import MagicMock

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
    'project11', 'project11.nav',
    'project11_msgs', 'project11_msgs.msg',
    'std_msgs', 'std_msgs.msg',
    # Other external
    'marine_nav_tasks',
]

for _mod in _MOCK_MODULES:
    sys.modules.setdefault(_mod, MagicMock())
