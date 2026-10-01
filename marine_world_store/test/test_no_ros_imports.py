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

"""
The library must stay importable without ROS.

``marine_world_store`` is a plain Python package with a ``package.xml`` shim
(#397, plan step 8): the day these tools are useful to someone outside ROS,
publishing is the package as it stands minus ``package.xml``. That is only true
while nothing in it reaches for a ROS runtime, so this is a test rather than a
sentence in the README.
"""

import ast
from pathlib import Path

#: Top-level module names that would make the package need a ROS runtime.
FORBIDDEN_ROOTS = {
    'rclpy',
    'rclcpp',
    'ament_index_python',
    'rosidl_runtime_py',
    'rosbag2_py',
    'launch',
    'launch_ros',
    'std_msgs',
    'geometry_msgs',
    'sensor_msgs',
    'marine_interfaces',
    'marine_acoustic_msgs',
}

#: Prefixes that are forbidden as a family (``ament_copyright``, ``ament_*``).
FORBIDDEN_PREFIXES = ('ament_',)


def _source_files(module_dir: Path):
    return sorted(p for p in module_dir.rglob('*.py'))


def _is_forbidden(name: str) -> bool:
    root = name.split('.')[0]
    return root in FORBIDDEN_ROOTS or root.startswith(FORBIDDEN_PREFIXES)


def test_source_files_found(module_dir: Path):
    """Guard the guard: an empty file list would pass vacuously."""
    assert len(_source_files(module_dir)) >= 2


def test_no_ros_imports(module_dir: Path):
    """No module under ``marine_world_store/`` imports a ROS package."""
    offenders = []
    for path in _source_files(module_dir):
        tree = ast.parse(path.read_text(), filename=str(path))
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom):
                # A relative import (level > 0) names no top-level package.
                names = [node.module] if node.level == 0 and node.module else []
            else:
                continue
            for name in names:
                if _is_forbidden(name):
                    offenders.append(f'{path.name}:{node.lineno}: {name}')
    assert not offenders, (
        'marine_world_store is a plain Python package (#397 plan step 8) and '
        'must import no ROS runtime:\n  ' + '\n  '.join(offenders))


def test_no_ros_install_space_reads(module_dir: Path):
    """Nor may it look data files up through the ROS install space."""
    offenders = []
    for path in _source_files(module_dir):
        text = path.read_text()
        for marker in ('get_package_share_directory', 'AMENT_PREFIX_PATH'):
            if marker in text:
                offenders.append(f'{path.name}: {marker}')
    assert not offenders, (
        'Reading the ROS install space would make the package unusable off a '
        'ROS host:\n  ' + '\n  '.join(offenders))


def test_every_module_imports_without_ros(module_dir, package_dir):
    """
    Every module here imports with no ROS environment sourced.

    Discovered rather than listed, so a new module is covered the day it is
    added. A module whose *third-party* library is not installed on this host
    (``pystac``, until ``rosdep install`` runs) skips with that reason; any
    other import error is a failure.
    """
    import importlib
    import sys

    sys.path.insert(0, str(package_dir))
    optional = {'pystac', 'snakemake'}
    imported = 0
    for path in _source_files(module_dir):
        rel = path.relative_to(package_dir).with_suffix('')
        name = '.'.join(rel.parts)
        if name.endswith('.__init__'):
            name = name[:-len('.__init__')]
        try:
            importlib.import_module(name)
        except ModuleNotFoundError as exc:
            if exc.name in optional:
                continue
            raise
        imported += 1
    assert imported, 'no module could be imported at all'
