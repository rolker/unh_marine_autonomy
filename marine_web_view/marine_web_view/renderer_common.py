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

"""Helpers shared by the renderers in this package.

Three things live here, each because a second copy would be a correctness
risk rather than a mere duplication:

* the ENU-yaw-to-compass conversion (REP-103). ROS measures yaw
  counter-clockwise from EAST while a compass heading runs clockwise from
  NORTH, so the conversion is `90 - yaw` -- easy to write, easy to write
  backwards, and a backwards copy renders a vessel pointing the wrong way
  with nothing failing. `state_renderer` had the only copy; `ais_renderer`
  needs the same conversion for AIS heading and course over ground, which
  would have made three (`ais_parser` builds the twist through the mirror of
  it upstream, in another repo).
* the atomic local write. The dry-run artifacts are served straight off disk
  by a plain `http.server` that a viewer polls, so a half-written file is a
  file a viewer parses.
* the timer-period sanity check. `create_timer` raises on a non-positive
  period, and it raises from a constructor that has already stood up an
  upload worker -- so an operator typo on the parameter every renderer
  documents as "the cost lever" takes the node down and leaves a thread
  behind it. `coverage_renderer` grew this first and owned the only copy.
"""

import math
import os


# Ceiling on a timer period. A day: long past any interval anybody means, and
# short of the values that are really a typo (a millisecond count pasted into
# a seconds field, say). Belongs with the check rather than with either node.
MAX_INTERVAL_SECONDS = 86400.0


def compass_degrees(yaw_radians):
    """Return a compass heading in degrees true from an ENU yaw in radians.

    ROS uses ENU (REP-103): yaw is counter-clockwise from EAST, a compass
    heading is clockwise from NORTH -- hence 90 - yaw. Verified in simulation
    against course made good.
    """
    return (90.0 - math.degrees(yaw_radians)) % 360.0


def yaw_from_quaternion(q):
    """Return the ENU yaw in radians of a geometry_msgs/Quaternion."""
    return math.atan2(2.0 * (q.w * q.z + q.x * q.y),
                      1.0 - 2.0 * (q.y * q.y + q.z * q.z))


def heading_from_quaternion(q):
    """Return a compass heading in degrees true, or None without a quaternion.

    A caller that has no orientation at all passes None and gets None back,
    which is the JSON `null` the page reads as "heading unknown". Note that
    this cannot tell an ABSENT heading from one that happens to be due east:
    the identity quaternion is a perfectly valid orientation. Callers whose
    source distinguishes the two (AIS does, in the yaw covariance) must make
    that decision before calling here.
    """
    if q is None:
        return None
    return compass_degrees(yaw_from_quaternion(q))


def write_atomic(path, payload):
    """Write payload to path atomically via a same-dir temp + os.replace.

    The dry-run artifacts are served straight off disk by a plain
    http.server that a viewer polls, so a reader must never observe a
    half-written file. os.replace is atomic within a filesystem, and the
    temp file is a sibling of the target so the rename stays on it.
    """
    tmp = '{}.tmp.{}'.format(path, os.getpid())
    with open(tmp, 'w') as handle:
        handle.write(payload)
    os.replace(tmp, path)


def sane_interval(value, default):
    """Return `(seconds, True)`, or `(default, False)` if unusable."""
    try:
        seconds = float(value)
    except (TypeError, ValueError):
        return default, False
    if seconds > 0.0 and seconds <= MAX_INTERVAL_SECONDS:
        return seconds, True
    return default, False
