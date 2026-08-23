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

"""Track history and decimation over a realistically long window.

Every manual and simulated run of this node during development lasted minutes,
so the track window never filled and the trim/decimate paths never engaged --
which is precisely how an O(n) full-list rebuild in the subscriber callback
survived two review rounds. These tests seed a multi-hour history directly so
the long-window behaviour is covered without a long-running test.
"""

from collections import deque
import math

from marine_web_view.state_renderer import decimate_track, TRACK_BANDS


def _lawnmower(duration_s, rate_hz=1.0, speed=2.0, leg=400.0, spacing=40.0):
    """Build a synthetic survey track as (stamp, lat, lon) tuples.

    1 Hz by default: enough to fill a multi-hour window (the point of these
    tests) without making the suite slow. The trim and decimate paths care
    about window span, not sample rate.
    """
    lat0, lon0 = 43.07, -70.71
    m_lat = 1.0 / 111319.49
    m_lon = 1.0 / (111319.49 * math.cos(math.radians(lat0)))
    points = deque()
    t = 0.0
    x = 0.0
    y = 0.0
    direction = 1
    step = 1.0 / rate_hz
    while t < duration_s:
        x += speed * direction * step
        if abs(x) > leg:
            x = leg * direction
            y += spacing
            direction *= -1
        points.append((t, lat0 + y * m_lat, lon0 + x * m_lon))
        t += step
    return points


def test_decimate_accepts_a_deque():
    """History is a deque (O(1) trim); decimate_track must handle it."""
    points = _lawnmower(600.0)
    assert isinstance(points, deque)
    out = decimate_track(points, points[-1][0])
    assert len(out) >= 2


def test_decimate_reduces_a_long_track():
    """A four-hour track must collapse hard, or the artifact is unshippable."""
    points = _lawnmower(4 * 3600.0)
    out = decimate_track(points, points[-1][0])
    assert len(out) < len(points) / 10, (
        'expected >10x reduction, got {} from {}'.format(len(out), len(points)))


def test_decimate_respects_band_tolerances():
    """Each age band must stay within its stated tolerance.

    This is the property that makes decimation safe: the drawn line may shed
    points but must not wander off the path the vessel actually took.
    """
    points = _lawnmower(4 * 3600.0)
    now = points[-1][0]
    out = decimate_track(points, now)

    def local(p):
        return (p[2] * 111319.49 * math.cos(math.radians(43.07)),
                p[1] * 111319.49)

    line = [local(p) for p in out]

    def distance_to_segment(px, py, ax, ay, bx, by):
        dx = bx - ax
        dy = by - ay
        if dx == 0.0 and dy == 0.0:
            return math.hypot(px - ax, py - ay)
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy)
                         / (dx * dx + dy * dy)))
        return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

    # Sample rather than check all ~14k points against ~600 segments: the
    # nearest-segment search is O(n*m) and the property holds pointwise, so a
    # 1-in-10 sample exercises it without a minute-long unit test.
    worst = {}
    for point in list(points)[::10]:
        age = now - point[0]
        band = next((limit for limit, _ in TRACK_BANDS if age <= limit), None)
        if band is None:
            continue
        px, py = local(point)
        best = min(distance_to_segment(px, py, *line[i], *line[i + 1])
                   for i in range(len(line) - 1))
        worst[band] = max(worst.get(band, 0.0), best)

    for limit, tolerance in TRACK_BANDS:
        if limit not in worst:
            continue
        assert worst[limit] <= tolerance + 1.5, (
            'band <= {}s: deviation {:.2f} m exceeds tolerance {} m'.format(
                limit, worst[limit], tolerance))


def test_oldest_band_is_a_catch_all():
    """Nothing inside track_seconds may fall outside every band.

    The bands are a fixed module constant while track_seconds is a parameter;
    if the oldest band stops being open-ended, a longer configured track
    silently renders nothing beyond the last band.
    """
    oldest_limit = TRACK_BANDS[-1][0]
    assert oldest_limit == float('inf') or oldest_limit >= 14400.0
