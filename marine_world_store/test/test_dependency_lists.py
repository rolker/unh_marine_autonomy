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
One dependency list, written twice -- this fails when the two drift.

``package.xml`` names rosdep keys (what ``rosdep install`` resolves on a ROS
host); ``setup.cfg`` names distribution names (what ``pip install .`` resolves
anywhere else). They describe the same libraries, so a dependency added to one
and forgotten in the other is a defect, not a difference.
"""

import configparser
from pathlib import Path
import xml.etree.ElementTree as ET

#: rosdep key -> the distribution name that installs the same library.
#: Adding a dependency means adding a row here; an unmapped key fails the test
#: rather than being skipped, so the table cannot rot into a silent allowlist.
KEY_TO_DISTRIBUTION = {
    'python3-yaml': 'PyYAML',
    'python3-gdal': 'GDAL',
    'python3-numpy': 'numpy',
    'python3-pystac': 'pystac',
    'snakemake': 'snakemake',
}

#: Build-system requirements, which are not runtime libraries and are declared
#: by setuptools itself rather than by either list.
NOT_A_RUNTIME_LIBRARY = {'setuptools'}


def _normalize(name: str) -> str:
    """PEP 503 normalization, so PyYAML and pyyaml compare equal."""
    return name.lower().replace('_', '-').replace('.', '-')


def _exec_depends(package_dir: Path):
    root = ET.parse(package_dir / 'package.xml').getroot()
    return {e.text.strip() for e in root.findall('exec_depend')}


def _install_requires(package_dir: Path):
    parser = configparser.ConfigParser()
    parser.read(package_dir / 'setup.cfg')
    raw = parser.get('options', 'install_requires', fallback='')
    names = set()
    for line in raw.splitlines():
        line = line.strip()
        if not line:
            continue
        # Strip any version specifier: "pystac >= 1.9" -> "pystac".
        name = line.split(';')[0].strip()
        for sep in ('>=', '<=', '==', '!=', '~=', '>', '<', '['):
            name = name.split(sep)[0].strip()
        if name not in NOT_A_RUNTIME_LIBRARY:
            names.add(name)
    return names


def test_every_rosdep_key_is_mapped(package_dir: Path):
    """A new <exec_depend> must gain a row in the mapping table."""
    unmapped = _exec_depends(package_dir) - set(KEY_TO_DISTRIBUTION)
    assert not unmapped, (
        'package.xml declares rosdep keys with no distribution-name mapping: '
        f'{sorted(unmapped)}. Add them to KEY_TO_DISTRIBUTION (and to '
        'setup.cfg install_requires).')


def test_lists_agree(package_dir: Path):
    """The two dependency lists name the same libraries."""
    from_package_xml = {
        _normalize(KEY_TO_DISTRIBUTION[k]) for k in _exec_depends(package_dir)
        if k in KEY_TO_DISTRIBUTION}
    from_setup_cfg = {_normalize(n) for n in _install_requires(package_dir)}
    assert from_package_xml == from_setup_cfg, (
        'package.xml <exec_depend>s and setup.cfg install_requires disagree.\n'
        f'  only in package.xml: {sorted(from_package_xml - from_setup_cfg)}\n'
        f'  only in setup.cfg:   {sorted(from_setup_cfg - from_package_xml)}')


def test_lists_are_not_empty(package_dir: Path):
    """Guard the guard: two empty lists would agree vacuously."""
    assert _exec_depends(package_dir)
    assert _install_requires(package_dir)


def test_console_scripts_are_declared(package_dir: Path):
    """Every CLI module has a console_scripts entry point."""
    parser = configparser.ConfigParser()
    parser.read(package_dir / 'setup.cfg')
    declared = parser.get('options.entry_points', 'console_scripts',
                          fallback='')
    targets = {line.split('=')[1].strip().split(':')[0]
               for line in declared.splitlines() if '=' in line}
    modules = {
        f'marine_world_store.cli.{p.stem}'
        for p in (package_dir / 'marine_world_store' / 'cli').glob('mws_*.py')}
    assert modules, 'no CLI modules found'
    assert modules == targets, (
        'CLI modules and console_scripts entry points disagree.\n'
        f'  modules with no entry point: {sorted(modules - targets)}\n'
        f'  entry points with no module: {sorted(targets - modules)}')
