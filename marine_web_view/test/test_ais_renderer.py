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

"""Bind what ais_renderer publishes, and what it refuses to publish.

The failure this file exists for first is the shared one: the artifact is ONE
object read by every viewer, so a single malformed contact is not a degraded
marker, it is a blank AIS layer for everybody. `json.dumps` emits a bare `NaN`
token for a NaN float and `JSON.parse` rejects it outright -- and ais_parser
writes NaN lat/lon whenever a position report carries none.

The rest is the shape the page is written against: heading present only when
one was actually reported, hull dimensions in the form `hullShape()` already
takes, and an unchanged contact set costing no upload at all.

Discovery is turned off and the domain moved aside so a test run cannot reach
a renderer running on the same host (the same treatment, and for the same
reason, as test_dry_run_needs_no_aws).
"""

import json
import math
import os

os.environ['ROS_DOMAIN_ID'] = '101'
os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'OFF'

from marine_ais_msgs.msg import AISContact       # noqa: E402

from marine_web_view import ais_renderer         # noqa: E402
from marine_web_view.ais_renderer import heading_degrees      # noqa: E402
from marine_web_view.ais_renderer import speed_and_course     # noqa: E402
from marine_web_view.ais_renderer import vessel_dimensions    # noqa: E402

import pytest                                    # noqa: E402

import rclpy                                     # noqa: E402


HEADING_VARIANCE = math.radians(5.0) ** 2   # what the tracker writes
UNKNOWN_VARIANCE = 1.0e6                    # the tracker's "not reported"


def _contact(mmsi=366000001, latitude=43.08, longitude=-70.75, stamp=1000.0,
             heading_deg=None, sog_knots=None, cog_deg=None, name='',
             callsign='', dimensions=None):
    """Return an AISContact shaped the way ais_contact_tracker publishes one."""
    contact = AISContact()
    contact.id = mmsi
    contact.header.stamp.sec = int(stamp)
    contact.header.stamp.nanosec = int(round((stamp - int(stamp)) * 1e9))
    contact.pose.position.latitude = latitude
    contact.pose.position.longitude = longitude
    covariance = [0.0] * 36
    covariance[35] = UNKNOWN_VARIANCE
    if heading_deg is None:
        # What ais_parser leaves behind: the NULL quaternion, plus the
        # unknown-variance stand-in in the yaw covariance.
        contact.pose.orientation.w = 0.0
    else:
        yaw = math.radians(90.0 - heading_deg)
        contact.pose.orientation.z = math.sin(yaw / 2.0)
        contact.pose.orientation.w = math.cos(yaw / 2.0)
        covariance[35] = HEADING_VARIANCE
    contact.covariance = covariance
    if sog_knots is None or cog_deg is None:
        contact.twist.twist.linear.x = math.nan
        contact.twist.twist.linear.y = math.nan
    else:
        speed = sog_knots * ais_renderer.KNOT
        course = math.radians(90.0 - cog_deg)
        contact.twist.twist.linear.x = math.cos(course) * speed
        contact.twist.twist.linear.y = math.sin(course) * speed
    contact.static_info.name = name
    contact.static_info.callsign = callsign
    if dimensions is not None:
        bow, stern, port, starboard = dimensions
        contact.static_info.reference_to_bow_distance = bow
        contact.static_info.reference_to_stern_distance = stern
        contact.static_info.reference_to_port_distance = port
        contact.static_info.reference_to_starboard_distance = starboard
    return contact


@pytest.fixture
def node(tmp_path):
    """Return a dry-run renderer writing into tmp_path, with no S3 client."""
    rclpy.init(args=[
        '--ros-args',
        '-p', 'dry_run:=true',
        '-p', 'local_path:={}/ais.geojson'.format(tmp_path),
    ])
    renderer = ais_renderer.AisRenderer()
    # The clock the expiry is measured against, pinned so the tests are not
    # racing wall time.
    renderer._now = lambda: 1000.0
    try:
        yield renderer
    finally:
        renderer.stop()
        renderer.destroy_node()
        rclpy.shutdown()


def _published(node):
    """Return the parsed artifact the node last wrote, via a real tick."""
    node._tick()
    with open(node.local_path) as handle:
        return json.loads(handle.read())


def test_a_contact_without_a_position_never_reaches_the_artifact(node):
    """The one that breaks the page for EVERY viewer, not just one marker.

    ais_parser writes math.nan lat/lon when a position report carries none
    and the tracker publishes the pose anyway. json.dumps would render that
    as a bare NaN token, which JSON.parse rejects -- so the whole AIS layer
    would go blank on one silent beacon.
    """
    node._on_contact(_contact(mmsi=1, latitude=math.nan, longitude=math.nan))
    node._on_contact(_contact(mmsi=2, latitude=43.08, longitude=-70.75))
    artifact = _published(node)
    assert [f['properties']['mmsi'] for f in artifact['features']] == [2]
    with open(node.local_path) as handle:
        assert 'NaN' not in handle.read(), (
            'a bare NaN token in the artifact: JSON.parse rejects the whole '
            'object, so every viewer loses the AIS layer')


def test_a_positionless_contact_is_logged_once_not_per_message(node):
    """A silent beacon reports every few seconds for as long as it is on."""
    lines = []
    node.get_logger().info = lambda message, **kwargs: lines.append(message)
    for _ in range(20):
        node._on_contact(_contact(mmsi=7, latitude=math.nan,
                                  longitude=math.nan))
    assert len([line for line in lines if 'MMSI 7' in line]) == 1, lines


def test_a_contact_that_regains_a_position_is_reported_again_if_it_is_lost(
        node):
    """The once-per-MMSI record must not outlive the condition it describes."""
    lines = []
    node.get_logger().info = lambda message, **kwargs: lines.append(message)
    node._on_contact(_contact(mmsi=7, latitude=math.nan, longitude=math.nan))
    node._on_contact(_contact(mmsi=7))
    node._on_contact(_contact(mmsi=7, latitude=math.nan, longitude=math.nan))
    assert len([line for line in lines if 'MMSI 7' in line]) == 2, lines


def test_the_serialiser_refuses_a_nan_that_slips_past_the_position_check(node):
    """`allow_nan=False` is the backstop, and it has to actually be set.

    The position check is the guard; this is what makes a future path that
    reintroduces a NaN somewhere else fail loudly in a process with a log,
    rather than silently on everyone's page.
    """
    with pytest.raises(ValueError):
        node._geojson([{'properties': {'speed_knots': float('nan')}}])


def test_every_contact_is_one_object(node):
    """One FeatureCollection for all contacts, not one object each."""
    for mmsi in range(10):
        node._on_contact(_contact(mmsi=366000000 + mmsi))
    artifact = _published(node)
    assert artifact['type'] == 'FeatureCollection'
    assert len(artifact['features']) == 10
    assert artifact['properties']['contacts'] == 10
    assert os.listdir(os.path.dirname(node.local_path)) == ['ais.geojson']


def test_an_unchanged_contact_set_costs_no_write(node):
    """S3 PUTs bill per request; an idle river must cost nothing."""
    node._on_contact(_contact())
    node._tick()
    assert node._writes == 1
    for _ in range(5):
        node._on_contact(_contact())      # same report, re-heard
        node._tick()
    assert node._writes == 1, 'an unchanged contact set was uploaded again'
    assert node._skipped == 5

    node._on_contact(_contact(latitude=43.09))
    node._tick()
    assert node._writes == 2, 'a moved contact was not published'
    assert node._skipped == 0


def test_a_newly_arrived_name_publishes(node):
    """The "position but no name" case resolving is a visible change."""
    node._on_contact(_contact())
    node._tick()
    assert _published(node)['features'][0]['properties']['name'] is None
    node._on_contact(_contact(name='TUG ARTHUR', callsign='WDE1234'))
    artifact = _published(node)
    assert artifact['features'][0]['properties']['name'] == 'TUG ARTHUR'
    assert artifact['features'][0]['properties']['callsign'] == 'WDE1234'
    assert node._writes == 2


def test_an_at_padded_name_is_not_a_name():
    """ITU-R M.1371 pads name and call sign with '@' for "not available"."""
    assert ais_renderer._clean('@@@@@@@@@@@@@@@@@@@@') is None
    assert ais_renderer._clean('') is None
    assert ais_renderer._clean('TUG ARTHUR@@@@@@@@@') == 'TUG ARTHUR'


def test_a_contact_unheard_for_the_timeout_is_dropped_and_forgotten(node):
    """Expiry is pruning, not filtering: memory is bounded on a long watch."""
    node._on_contact(_contact(mmsi=1, stamp=1000.0))
    node._on_contact(_contact(mmsi=2, stamp=500.0))     # 500 s old
    node._on_contact(_contact(mmsi=3, stamp=100.0))     # 900 s old
    assert node.contact_timeout == 600.0
    artifact = _published(node)
    assert [f['properties']['mmsi'] for f in artifact['features']] == [1, 2]
    assert sorted(node._contacts) == [1, 2], (
        'the expired contact is still held: memory grows for the life of '
        'the node')


def test_an_expired_contact_that_reappears_carries_no_stale_state(node):
    """A fresh entry, not a resurrection of the one that aged out."""
    node._on_contact(_contact(mmsi=1, stamp=100.0, name='OLD NAME'))
    node._tick()
    assert node._contacts == {}
    node._on_contact(_contact(mmsi=1, stamp=1000.0))
    artifact = _published(node)
    assert artifact['features'][0]['properties']['name'] is None


def test_the_hull_block_is_what_the_pages_hullshape_expects():
    """A/B/C/D -> length/width/reference offsets, mirroring PlatformList.

    The page draws AIS contacts with the SAME hullShape() it draws BizzyBoat
    with, so the conversion has to land in that function's own terms:
    hullShape computes toBow = length / 2 - reference_x, and toBow is exactly
    AIS's A.
    """
    static = _contact(dimensions=(30, 10, 4, 6)).static_info
    vessel = vessel_dimensions(static)
    assert vessel['length'] == 40.0
    assert vessel['width'] == 10.0
    # Reproduce hullShape()'s own arithmetic and land back on A/B/C/D.
    assert vessel['length'] / 2 - vessel['reference_x'] == 30.0   # A, bow
    assert vessel['length'] / 2 + vessel['reference_x'] == 10.0   # B, stern
    assert vessel['width'] / 2 - vessel['reference_y'] == 4.0     # C, port
    assert vessel['width'] / 2 + vessel['reference_y'] == 6.0     # D, starboard


def test_no_dimensions_reported_is_an_all_zero_hull():
    """A = B = C = D = 0 is the standard's "not available".

    hullShape() already falls back to its triangle or circle on a zero
    max(length, width), so nothing extra is needed on the page -- but a
    non-zero block computed from nothing would draw a confident wrong hull.
    """
    vessel = vessel_dimensions(_contact().static_info)
    assert vessel == {'length': 0.0, 'width': 0.0,
                      'reference_x': 0.0, 'reference_y': 0.0}


def test_an_unreported_heading_is_not_a_heading_of_due_east():
    """The quaternion cannot answer this; the yaw covariance can.

    An unreported AIS true heading leaves the identity quaternion behind in
    some paths and the null quaternion in others, and the identity is a
    perfectly valid yaw of 0 -- due east in ENU. ais_contact_tracker records
    what was actually reported in covariance[35], which is what ais_layer.cpp
    tests too.
    """
    threshold = ais_renderer.DEFAULT_HEADING_VARIANCE_THRESHOLD
    assert heading_degrees(_contact(), threshold) is None

    # The identity quaternion, which reads as a good "due east" on its own.
    identity = _contact()
    identity.pose.orientation.w = 1.0
    assert heading_degrees(identity, threshold) is None, (
        'an identity quaternion with the unknown-variance stand-in was read '
        'as a heading of due east')

    assert heading_degrees(_contact(heading_deg=45.0), threshold) == \
        pytest.approx(45.0)
    assert heading_degrees(_contact(heading_deg=270.0), threshold) == \
        pytest.approx(270.0)


def test_the_heading_threshold_is_a_decade_below_the_trackers_sentinel():
    """It must still read as unknown if the two configurations drift."""
    assert ais_renderer.DEFAULT_HEADING_VARIANCE_THRESHOLD == 1.0e5
    assert UNKNOWN_VARIANCE > ais_renderer.DEFAULT_HEADING_VARIANCE_THRESHOLD


def test_speed_and_course_come_back_out_in_the_units_they_went_in():
    """ais_parser encodes SOG/COG as an ENU velocity; this reverses it."""
    speed, course = speed_and_course(_contact(sog_knots=8.4, cog_deg=125.0))
    assert speed == pytest.approx(8.4, abs=0.05)
    assert course == pytest.approx(125.0, abs=0.05)


def test_an_absent_velocity_is_null_not_zero():
    """A NaN in the twist means missing, not "stopped"."""
    assert speed_and_course(_contact()) == (None, None)


def test_a_stationary_contact_reports_no_course():
    """At zero speed the course is gone, not east.

    ais_parser encodes course as the DIRECTION of the velocity vector, so at
    a SOG of zero both components are zero and atan2(0, 0) is 0.0 -- which
    would be published as a confident heading of due east.
    """
    speed, course = speed_and_course(_contact(sog_knots=0.0, cog_deg=125.0))
    assert speed == 0.0
    assert course is None


def test_the_artifact_carries_what_the_popup_renders(node):
    """The page's popup is written against these names."""
    node._on_contact(_contact(name='TUG ARTHUR', callsign='WDE1234',
                              heading_deg=310.0, sog_knots=6.0,
                              cog_deg=308.0, dimensions=(30, 10, 4, 6)))
    properties = _published(node)['features'][0]['properties']
    for field in ('mmsi', 'name', 'callsign', 'ship_and_cargo_type',
                  'navigational_status', 'speed_knots', 'course_deg',
                  'heading_deg', 'stamp', 'stamp_iso', 'vessel'):
        assert field in properties, field
    assert properties['role'] == 'ais'


def test_coordinates_are_longitude_first(node):
    """Coordinates are [lon, lat] in GeoJSON and [lat, lng] in Leaflet.

    Reversing them silently puts every contact in the wrong hemisphere.
    """
    node._on_contact(_contact(latitude=43.08, longitude=-70.75))
    coordinates = _published(node)['features'][0]['geometry']['coordinates']
    assert coordinates == [-70.75, 43.08]


def test_contacts_are_ordered_so_the_change_check_means_something(node):
    """Dict order follows arrival; an unordered list changes on every tick."""
    for mmsi in (900, 100, 500):
        node._on_contact(_contact(mmsi=mmsi))
    node._tick()
    assert node._writes == 1
    # Re-heard in a different order, nothing about them changed.
    for mmsi in (100, 500, 900):
        node._on_contact(_contact(mmsi=mmsi))
    node._tick()
    assert node._writes == 1, (
        'the same contacts re-heard in another order counted as a change: '
        'every tick would pay an S3 PUT')
