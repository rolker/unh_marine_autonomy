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

"""Pin the promise that a dry run needs no AWS access at all.

Both nodes gate their uploader construction on `dry_run`, three docstrings
and the README say so, and the simulator workflow plus this whole test suite
depend on it -- yet the gate could be deleted from BOTH nodes without failing
anything. This module is that gate's only test.

It is the one place here that builds the real ROS nodes, because the gate
lives in `__init__` and nothing short of running it can bind it. Discovery is
turned off and the domain moved aside so a test run cannot reach a renderer
running on the same host.
"""

import os

os.environ['ROS_DOMAIN_ID'] = '101'
os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'OFF'

import rclpy                                            # noqa: E402

from marine_web_view import coverage_renderer           # noqa: E402
from marine_web_view import state_renderer              # noqa: E402

import pytest                                           # noqa: E402


class _Detonator:
    """An uploader that fails the test if it is ever constructed."""

    def __init__(self, *args, **kwargs):
        raise AssertionError(
            'a dry-run node constructed an S3 client: a simulator host with '
            'no credentials would now fail at startup, and this package '
            'documents dry_run as needing no AWS access at all')


@pytest.fixture
def dry_run_node(tmp_path, monkeypatch):
    """Start rclpy with dry_run overridden, and no uploader available."""
    monkeypatch.setattr(coverage_renderer, 'S3Uploader', _Detonator)
    monkeypatch.setattr(state_renderer, 'S3Uploader', _Detonator)
    rclpy.init(args=[
        '--ros-args',
        '-p', 'dry_run:=true',
        '-p', 'local_dir:={}'.format(tmp_path),
        '-p', 'local_path:={}/position.geojson'.format(tmp_path),
        '-p', 'track_local_path:={}/track.geojson'.format(tmp_path),
    ])
    try:
        yield
    finally:
        rclpy.shutdown()


def test_coverage_renderer_builds_no_client_on_a_dry_run(dry_run_node):
    """`dry_run` plus `local_dir` is the simulator's whole configuration."""
    node = coverage_renderer.CoverageRenderer()
    try:
        assert node._uploader is None, (
            'the dry-run gate no longer keeps the uploader unbuilt')
    finally:
        node.stop()
        node.destroy_node()


def test_state_renderer_builds_no_client_on_a_dry_run(dry_run_node):
    """Sharpest here: this node does NOT coalesce an empty profile.

    Without the gate, a credential-free host raises ProfileNotFound out of
    `__init__` -- the node never starts at all, rather than failing per
    upload.
    """
    node = state_renderer.StateRenderer()
    try:
        assert node._uploader is None, (
            'the dry-run gate no longer keeps the uploader unbuilt')
    finally:
        node.destroy_node()
