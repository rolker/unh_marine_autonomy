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
from marine_ais_msgs.msg import NavigationalStatus  # noqa: E402

from marine_web_view import ais_renderer         # noqa: E402
from marine_web_view.ais_renderer import excluded_from_public  # noqa: E402
from marine_web_view.ais_renderer import heading_degrees      # noqa: E402
from marine_web_view.ais_renderer import speed_and_course     # noqa: E402
from marine_web_view.ais_renderer import vessel_dimensions    # noqa: E402

import pytest                                    # noqa: E402

import rclpy                                     # noqa: E402


HEADING_VARIANCE = math.radians(5.0) ** 2   # what the tracker writes
UNKNOWN_VARIANCE = 1.0e6                    # the tracker's "not reported"


def _contact(mmsi=366000001, latitude=43.08, longitude=-70.75, stamp=1000.0,
             heading_deg=None, sog_knots=None, cog_deg=None, name='',
             callsign='', dimensions=None, nav_status=0, ship_type=0,
             message_id=1):
    """Return an AISContact shaped the way ais_contact_tracker publishes one."""
    contact = AISContact()
    contact.id = mmsi
    contact.position_message_id = message_id
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
    contact.navigational_status.status = nav_status
    contact.static_info.ship_and_cargo_type = ship_type
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


def test_a_failed_pass_costs_one_tick_not_the_node(node, tmp_path):
    """A raise out of a timer callback unwinds spin() and the process exits.

    The most reachable one is an OSError from the dry-run write: a
    `local_path` whose directory does not exist, or a full disk. Losing the
    renderer over a missing directory is not a trade anyone would pick on a
    shore watch, and the artifact's own "breaks one upload, in a process with
    a log" promise was only ever true of the payload.
    """
    errors = []
    node.get_logger().error = lambda message, **kwargs: errors.append(message)
    node.local_path = str(tmp_path / 'no-such-directory' / 'ais.geojson')
    node._on_contact(_contact())
    node._tick()                                   # must not raise
    assert node._writes == 0 and node._failures == 1
    assert errors and 'publish pass failed' in errors[0], errors
    assert 'FileNotFoundError' in errors[0], (
        'the log does not say what went wrong, so a contained failure is a '
        'silent one')

    # And the next pass still runs: the failure cost one tick, not the node.
    node.local_path = str(tmp_path / 'ais.geojson')
    node._tick()
    assert node._writes == 1


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
    """S3 PUTs bill per request; an EMPTY river must cost nothing.

    "Unchanged" here means the identical report re-delivered, stamp included
    -- which is what a bag replay or a duplicated message looks like, not what
    a live receiver produces. A contact genuinely re-heard carries a new stamp
    and does publish; that is the next test, and the two together are the
    whole cost model.
    """
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


def test_a_contact_re_heard_publishes_its_new_stamp(node):
    """The stamp is in the change signature on purpose, and it has to be.

    The page has no other liveness signal -- the collection's `generated`
    legitimately stops moving on a quiet river -- so it fades a contact whose
    stamp stops advancing. Drop the stamp from the signature to save the PUT
    and a vessel holding station fades out while it is still reporting
    perfectly well, which is the failure the fade exists to report.
    """
    node._on_contact(_contact(stamp=1000.0))
    node._tick()
    assert node._writes == 1
    node._on_contact(_contact(stamp=1010.0))   # same vessel, same place
    node._tick()
    assert node._writes == 2, (
        'a contact re-heard 10 s later did not republish: its stamp stops '
        'advancing on the page and the layer fades a live contact out')
    assert _published(node)['features'][0]['properties']['stamp'] == 1010.0


def test_an_empty_river_costs_nothing_at_all(node):
    """The floor of the cost model: nothing heard, nothing uploaded."""
    for _ in range(10):
        node._tick()
    assert node._writes <= 1, (
        'an empty contact set was uploaded more than once')


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


def test_the_held_contact_set_has_a_ceiling(node):
    """PUT count is contact-independent; object size and CDN egress are not.

    `contact_timeout` bounds the set in TIME, which is no bound at all against
    a burst: MMSI is an unauthenticated uint32 on the air, and every distinct
    one heard inside the timeout is another feature in an object every viewer
    downloads.
    """
    node.get_logger().warn = lambda message, **kwargs: None
    node.max_contacts = 3
    for mmsi, stamp in ((1, 950.0), (2, 960.0), (3, 970.0)):
        node._on_contact(_contact(mmsi=mmsi, stamp=stamp))
    node._on_contact(_contact(mmsi=4, stamp=980.0))
    assert sorted(node._contacts) == [2, 3, 4], (
        'the ceiling did not evict the contact closest to expiring')
    # A contact already held is an update, not an arrival: it must not evict.
    node._on_contact(_contact(mmsi=2, stamp=990.0))
    assert sorted(node._contacts) == [2, 3, 4]
    assert len(_published(node)['features']) == 3


def test_a_truncated_artifact_says_so(node):
    """A page quietly showing some of the traffic is the bad failure."""
    warnings = []
    node.get_logger().warn = lambda message, **kwargs: warnings.append(message)
    node.max_contacts = 1
    node._on_contact(_contact(mmsi=1, stamp=1000.0))
    node._on_contact(_contact(mmsi=2, stamp=1000.0))
    assert warnings and 'max_contacts' in warnings[0], warnings


def test_the_contact_ceiling_is_a_parameter(node):
    """Not a constant: what a busy receiver hears is a local question."""
    assert node.get_parameter('max_contacts').value == 500
    assert node.max_contacts == 500


def test_the_once_per_mmsi_log_records_are_bounded(node):
    """`_expire` bounds the contacts; nothing bounded the log records.

    MMSI is an unauthenticated uint32 on the air and these two sets have no
    timeout pruning them, so on the shore watch `_expire`'s docstring
    advertises they are what grows instead.
    """
    node.get_logger().info = lambda message, **kwargs: None
    cap = ais_renderer.MAX_REMEMBERED_MMSI
    for mmsi in range(cap + 500):
        node._on_contact(_contact(mmsi=mmsi, latitude=math.nan,
                                  longitude=math.nan))
        node._on_contact(_contact(mmsi=mmsi + 10 ** 6, nav_status=14))
    assert len(node._positionless) == cap
    assert len(node._withheld) == cap
    # The cap evicts the OLDEST record, so what a flood costs is a repeated
    # log line for a contact nobody has heard in thousands of contacts.
    assert 0 not in node._positionless
    assert cap + 499 in node._positionless


def test_a_name_cannot_carry_control_or_bidi_characters_to_the_popup(node):
    """The name comes off the air from anyone with a transmitter.

    The popup is built from text nodes, so markup is already closed off --
    but six-bit ASCII cannot carry a bidi override and the decoder in front of
    this can hand one over anyway. Those characters REORDER what follows them,
    so a name can be dressed to read in a public popup as something other than
    what was transmitted. An unpaired surrogate is the other half: json.dumps
    emits it and a strict parser rejects the whole object, which is the bare
    NaN failure again -- one contact, every viewer.
    """
    assert ais_renderer._clean('TUG\u202eARTHUR') == 'TUG ARTHUR'
    assert ais_renderer._clean('TUG\x00\x07ARTHUR') == 'TUG ARTHUR'
    assert ais_renderer._clean('\u200b\u200b\u200b') is None
    assert ais_renderer._clean('TUG\ud800') == 'TUG'
    # And an ordinary name is left exactly as it was.
    assert ais_renderer._clean('TUG ARTHUR') == 'TUG ARTHUR'

    node._on_contact(_contact(name='TUG\u202eARTHUR', callsign='WD\x01E1234'))
    properties = _published(node)['features'][0]['properties']
    assert properties['name'] == 'TUG ARTHUR'
    assert properties['callsign'] == 'WD E1234'


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


def test_a_distress_beacon_is_never_published(node):
    """Recorded operator decision (#357), and it runs in the RENDERER.

    Navigational status 14 is AIS-SART / MOB / EPIRB -- in the ordinary case a
    person in the water. The public page has no operational need for it, and
    filtering it on the page instead would still put the position in the
    bucket, on the CDN and in the page source. The only version that keeps it
    off a public network is the one that never uploads it, so the artifact is
    what this asserts against.
    """
    node._on_contact(_contact(mmsi=1, nav_status=14))
    node._on_contact(_contact(mmsi=2))
    artifact = _published(node)
    assert [f['properties']['mmsi'] for f in artifact['features']] == [2]
    assert 1 not in node._contacts, (
        'a withheld position is held in memory, one code path away from '
        'being published')


def test_a_responder_is_never_published(node):
    """Types 51 and 55: search and rescue, and law enforcement.

    A responder's own position says where a response is happening, and by
    inference what it is responding to, while it is happening.
    """
    node._on_contact(_contact(mmsi=51, ship_type=51))
    node._on_contact(_contact(mmsi=55, ship_type=55))
    node._on_contact(_contact(mmsi=52, ship_type=52))     # a tug, published
    node._on_contact(_contact(mmsi=50, ship_type=50))     # a pilot vessel
    artifact = _published(node)
    assert [f['properties']['mmsi'] for f in artifact['features']] == [50, 52]


def test_a_contact_that_later_declares_distress_is_withdrawn(node):
    """The filter has to run on every report, not only on the first.

    A vessel can hoist distress, or resolve to type 51 from a type 5 message,
    long after it was first heard -- and published.
    """
    node._on_contact(_contact(mmsi=1))
    assert [f['properties']['mmsi'] for f in _published(node)['features']] == [1]
    node._on_contact(_contact(mmsi=1, nav_status=14))
    artifact = _published(node)
    assert artifact['features'] == [], (
        'a contact that declared distress stayed on the public map')
    assert node._contacts == {}


def test_a_withheld_contact_is_logged_once_and_not_silently(node):
    """A filtered page must be tellable from a quiet one.

    Nothing else can say so: the artifact looks exactly the same as a river
    with nothing on it.
    """
    lines = []
    node.get_logger().info = lambda message, **kwargs: lines.append(message)
    for _ in range(20):
        node._on_contact(_contact(mmsi=7, nav_status=14))
    withheld = [line for line in lines if 'MMSI 7' in line]
    assert len(withheld) == 1, lines
    assert 'withheld' in withheld[0] and 'distress' in withheld[0], withheld


def test_a_contact_that_stands_down_is_reported_again_if_it_declares_again(
        node):
    """The once-per-MMSI record must not outlive the condition it describes."""
    lines = []
    node.get_logger().info = lambda message, **kwargs: lines.append(message)
    node._on_contact(_contact(mmsi=7, nav_status=14))
    node._on_contact(_contact(mmsi=7))
    node._on_contact(_contact(mmsi=7, nav_status=14))
    assert len([line for line in lines if 'withheld' in line]) == 2, lines


def test_the_filter_is_the_two_categories_the_operator_named(node):
    """Pinned against quiet broadening or narrowing of a policy decision."""
    assert ais_renderer.DISTRESS_NAV_STATUS == 14
    assert set(ais_renderer.RESPONDER_SHIP_TYPES) == {51, 55}
    assert excluded_from_public(_contact(nav_status=14)) is not None
    assert excluded_from_public(_contact(ship_type=51)) is not None
    assert excluded_from_public(_contact(ship_type=55)) is not None
    assert excluded_from_public(_contact()) is None
    assert excluded_from_public(_contact(nav_status=1)) is None


def test_a_distress_beacon_is_withheld_on_its_reserved_mmsi_range():
    """Status 14 is not the whole of a beacon's identity.

    Navigational status 14 is "AIS-SART (active)"; NavigationalStatus.msg
    itself records that status 15 -- undefined, the default a silent contact
    carries -- is "also used by AIS-SART, MOB-AIS and EPIRB-AIS under test".
    ITU reserves 970/972/974 for exactly these beacons, and that is the
    identifier that holds whether or not the beacon has declared.
    """
    assert set(ais_renderer.DISTRESS_MMSI_PREFIXES) == {970, 972, 974}
    for prefix in (970, 972, 974):
        beacon = _contact(mmsi=prefix * 1000000 + 123456, nav_status=15)
        assert excluded_from_public(beacon) is not None, prefix
    # An ordinary MMSI whose digits merely resemble one of the ranges must
    # still be published: the prefix is the first three digits of nine.
    assert excluded_from_public(_contact(mmsi=970123)) is None
    assert excluded_from_public(_contact(mmsi=367970123)) is None


def test_the_distress_status_is_taken_from_the_message_not_retyped():
    """One definition of 14, so the filter cannot drift from the interface."""
    assert (ais_renderer.DISTRESS_NAV_STATUS
            == NavigationalStatus.NAVIGATIONAL_STATUS_SART_MOB_EPIRB)


def test_a_sar_aircraft_is_withheld_on_the_only_identifier_it_carries():
    """The responder the ship-and-cargo-type test cannot see.

    AIS message 9 carries no navigational status -- ais_parser defaults it to
    15 -- and an aircraft never sends a type 5 or 24 static report, so
    `ship_and_cargo_type` stays 0 for its whole life. Both of the other two
    branches read as an ordinary vessel; the message id is the only thing
    that says otherwise, which is why ais_layer.cpp switches on it too.
    """
    assert ais_renderer.SAR_AIRCRAFT_MESSAGE_ID == 9
    aircraft = _contact(message_id=9, nav_status=15, ship_type=0)
    assert excluded_from_public(aircraft) is not None
    assert 'aircraft' in excluded_from_public(aircraft)
    # The status an aircraft's absent-status default shares with every other
    # contact that has not declared one must not withhold the whole river.
    assert excluded_from_public(_contact(nav_status=15)) is None
    # Class B (18/19) and class A (1/2/3) position reports stay published.
    for ordinary in (1, 2, 3, 18, 19, 27):
        assert excluded_from_public(_contact(message_id=ordinary)) is None


def test_a_withheld_aircraft_is_never_held_in_memory(node):
    """The filter runs in `_on_contact`, so nothing withheld is ever stored."""
    node.get_logger().info = lambda message, **kwargs: None
    node._on_contact(_contact(mmsi=111222333, message_id=9))
    assert node._contacts == {}


def test_a_replay_against_the_wrong_clock_announces_itself(node):
    """The silent failure the launch file's use_sim_time exists to prevent.

    Contact age is a header stamp subtracted from THIS NODE's clock. Replay a
    bag into a renderer left on the wall clock and every stamp is as old as
    the recording, so the whole set expires on the first tick and the artifact
    publishes `contacts: 0` -- no error, and a page that looks exactly like a
    quiet river. A contact that is already stale on arrival cannot come from a
    live receiver, so it is worth a word.
    """
    warnings = []
    node.get_logger().warn = lambda message, **kwargs: warnings.append(message)
    # _now() is pinned at 1000.0; a stamp from a bag recorded yesterday.
    for _ in range(5):
        node._on_contact(_contact(mmsi=1, stamp=1000.0 - 86400.0))
    node._on_contact(_contact(mmsi=2, stamp=1000.0 - 86400.0))
    assert len(warnings) == 1, warnings
    assert 'use_sim_time' in warnings[0] and '--clock' in warnings[0], (
        'the warning does not name the fix, so it is one more log line')
    assert _published(node)['properties']['contacts'] == 0, (
        'the reproduction itself changed; this test no longer covers it')


def test_a_live_contact_is_not_mistaken_for_a_clock_problem(node):
    """The warning must not fire on the ordinary case."""
    warnings = []
    node.get_logger().warn = lambda message, **kwargs: warnings.append(message)
    node._on_contact(_contact(mmsi=1, stamp=1000.0))
    node._on_contact(_contact(mmsi=2, stamp=1000.0 - 599.0))   # inside timeout
    assert warnings == []


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
