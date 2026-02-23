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

"""Unit tests for camp_interface utility functions (parseLatLong, listTasks).

These tests exercise the standalone utility functions in camp_interface.py
without requiring a running ROS2 system. ROS2 message types and marine
dependencies are mocked.
"""

import sys
import unittest
from unittest.mock import MagicMock, patch

# ---------------------------------------------------------------------------
# Mock every ROS / marine module that camp_interface.py imports so that
# the test can run without a ROS2 installation.
# ---------------------------------------------------------------------------
_MOCK_MODULES = [
    'rclpy', 'rclpy.lifecycle', 'rclpy.subscription',
    'geometry_msgs', 'geometry_msgs.msg',
    'marine_nav_interfaces', 'marine_nav_interfaces.msg',
    'marine_autonomy', 'marine_autonomy.nav',
    'marine_interfaces', 'marine_interfaces.msg',
    'std_msgs', 'std_msgs.msg',
]
_saved = {}
for _mod in _MOCK_MODULES:
    if _mod not in sys.modules:
        _saved[_mod] = None
        sys.modules[_mod] = MagicMock()
    else:
        _saved[_mod] = sys.modules[_mod]

# Now import the functions under test.
from mission_manager.camp_interface import listTasks, parseLatLong  # noqa: E402


def tearDownModule():
    """Restore sys.modules to avoid leaking mocks into other test files."""
    for mod, original in _saved.items():
        if original is None:
            sys.modules.pop(mod, None)
        else:
            sys.modules[mod] = original


class TestParseLatLong(unittest.TestCase):
    """Tests for the parseLatLong helper function."""

    def setUp(self):
        """Create a mock ROS node used by parseLatLong for logging."""
        self.node = MagicMock()
        self.logger = MagicMock()
        self.node.get_logger.return_value = self.logger

    # -- happy-path tests ---------------------------------------------------

    def test_valid_positive_values(self):
        """Parse two positive floats separated by a space."""
        result = parseLatLong('43.1 -70.9', self.node)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result['latitude'], 43.1)
        self.assertAlmostEqual(result['longitude'], -70.9)

    def test_valid_extra_whitespace(self):
        """Multiple whitespace characters between the two numbers."""
        result = parseLatLong('43.1   -70.9', self.node)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result['latitude'], 43.1)
        self.assertAlmostEqual(result['longitude'], -70.9)

    def test_valid_negative_latitude(self):
        """Both negative and positive values are accepted."""
        result = parseLatLong('-33.8688 151.2093', self.node)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result['latitude'], -33.8688)
        self.assertAlmostEqual(result['longitude'], 151.2093)

    def test_valid_zeroes(self):
        """Zero-zero is a valid coordinate (Gulf of Guinea)."""
        result = parseLatLong('0.0 0.0', self.node)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result['latitude'], 0.0)
        self.assertAlmostEqual(result['longitude'], 0.0)

    def test_valid_integer_strings(self):
        """Integer strings (no decimal point) are valid floats."""
        result = parseLatLong('43 -71', self.node)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result['latitude'], 43.0)
        self.assertAlmostEqual(result['longitude'], -71.0)

    # -- error-path tests ---------------------------------------------------

    def test_non_numeric_values(self):
        """Non-numeric tokens should return None and log an error."""
        result = parseLatLong('abc def', self.node)
        self.assertIsNone(result)
        self.logger.error.assert_called_once()

    def test_mixed_numeric_and_text(self):
        """One valid float and one non-numeric should return None."""
        result = parseLatLong('43.1 abc', self.node)
        self.assertIsNone(result)
        self.logger.error.assert_called_once()

    def test_single_element(self):
        """A single token cannot be split into lat and lon."""
        result = parseLatLong('43.1', self.node)
        self.assertIsNone(result)
        self.logger.info.assert_called_once()

    def test_three_elements(self):
        """Three tokens are too many."""
        result = parseLatLong('43.1 -70.9 100', self.node)
        self.assertIsNone(result)
        self.logger.info.assert_called_once()

    def test_empty_string(self):
        """An empty string yields an empty list after split()."""
        result = parseLatLong('', self.node)
        self.assertIsNone(result)


class TestListTasks(unittest.TestCase):
    """Tests for the listTasks helper that populates Heartbeat key-values."""

    @staticmethod
    def _make_kv(key='', value=''):
        """Lightweight stand-in for marine_interfaces.msg.KeyValue."""
        kv = MagicMock()
        kv.key = key
        kv.value = value
        return kv

    def _heartbeat(self):
        """Return a mock Heartbeat whose .values is a real list."""
        hb = MagicMock()
        hb.values = []
        return hb

    def test_none_tasks(self):
        """None input should produce a single 'tasks' -> 'none' entry."""
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks(None, hb)
        self.assertEqual(len(hb.values), 1)
        self.assertEqual(hb.values[0].key, 'tasks')
        self.assertEqual(hb.values[0].value, 'none')

    def test_empty_list(self):
        """An empty list should produce a single 'tasks' -> 'empty' entry."""
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks([], hb)
        self.assertEqual(len(hb.values), 1)
        self.assertEqual(hb.values[0].key, 'tasks')
        self.assertEqual(hb.values[0].value, 'empty')

    def _make_task(self, task_id='t', task_type='goto',
                   done=False, status=''):
        """Create a mock TaskInformation."""
        t = MagicMock()
        t.id = task_id
        t.type = task_type
        t.done = done
        t.status = status
        return t

    def test_single_task_basic(self):
        """A single active task produces one key-value with its type."""
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks([self._make_task('goto_0', 'goto')], hb)
        self.assertEqual(len(hb.values), 1)
        self.assertEqual(hb.values[0].key, 'goto_0')
        self.assertIn('type: goto', hb.values[0].value)
        self.assertNotIn('(done)', hb.values[0].value)

    def test_done_flag_shown(self):
        """A completed task should include '(done)' in the value."""
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks([self._make_task('h', 'hover', done=True)], hb)
        self.assertIn('(done)', hb.values[0].value)

    def test_status_shown(self):
        """A task with a non-empty status should include it in the value."""
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks(
                [self._make_task('s', 'survey_line', status='active')], hb)
        self.assertIn('status: active', hb.values[0].value)

    def test_multiple_tasks(self):
        """Multiple tasks each get their own key-value entry."""
        tasks = [self._make_task(f't{i}', 'goto') for i in range(4)]
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks(tasks, hb)
        self.assertEqual(len(hb.values), 4)

    def test_done_and_status_combined(self):
        """A task that is done AND has status shows both annotations."""
        hb = self._heartbeat()
        with patch('mission_manager.camp_interface.KeyValue', self._make_kv):
            listTasks(
                [self._make_task('x', 'goto', done=True, status='ok')], hb)
        val = hb.values[0].value
        self.assertIn('(done)', val)
        self.assertIn('status: ok', val)


if __name__ == '__main__':
    unittest.main()
