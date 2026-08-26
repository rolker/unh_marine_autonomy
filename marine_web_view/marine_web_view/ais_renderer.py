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

"""Render tracked AIS contacts to static GeoJSON for the public web view.

Third renderer in this package, and deliberately the same shape as the other
two: the same bucket/key/profile/dry_run/local_path/interval surface, the same
AsyncUploader upload path, the same "publish only when something changed" tick.
The public page shows BizzyBoat surrounded by nothing; this puts the traffic
around it on the map.

Source is `marine_ais_msgs/AISContact` on `/ais/contacts`, published on the
ROC desktop ashore by the nmea_relay -> ais_parser -> ais_contact_tracker
chain. The tracker republishes the whole accumulated contact on every position
report, so the newest message per MMSI is the whole truth about that contact
and there is no out-of-order history to reconcile (unlike state_renderer's
track).

Everything a viewer sees is one object: one FeatureCollection for all
contacts, written when the set or its contents change -- and a contact's
`stamp` is part of "its contents", because the page reads that stamp as the
AIS layer's only liveness signal. So an empty river costs zero S3 PUTs, while
one contact reporting on it costs up to one PUT per `interval`: the
documented ceiling, and the price of a page that can tell a dead renderer
from calm water.

Three things this node has to get right that are invisible in a good run:

* **A contact with no position.** ais_parser writes NaN lat/lon when a
  position report carries none, the tracker copies the pose and publishes it
  anyway, and `json.dumps` emits a bare `NaN` token that `JSON.parse` rejects
  outright -- so ONE such contact would break the AIS layer for every viewer.
  Those contacts are dropped here, and `allow_nan=False` sits behind that as a
  backstop so a future path that reintroduces it fails loudly in this process
  rather than silently on everyone's page.
* **"No heading" versus "heading due east."** An unreported AIS true heading
  leaves an identity (or null) quaternion behind, which reads as a perfectly
  good yaw of zero -- due east in ENU. The tracker records what was actually
  reported in the yaw covariance, so that is what is tested here, the same way
  ais_layer.cpp does for the costmap.
* **Who is not on the map.** Distress beacons and SAR/law-enforcement
  contacts -- aircraft included -- are dropped HERE rather than hidden on the
  page, so those positions never reach S3 at all. See `excluded_from_public`
  and the README.

See rolker/unh_marine_autonomy#357.
"""

import json
import math
import time
import unicodedata

from marine_ais_msgs.msg import AISContact
from marine_ais_msgs.msg import NavigationalStatus

from marine_web_view.renderer_common import compass_degrees
from marine_web_view.renderer_common import heading_from_quaternion
from marine_web_view.renderer_common import sane_interval
from marine_web_view.renderer_common import write_atomic
from marine_web_view.s3_upload import AsyncUploader
from marine_web_view.s3_upload import describe_error
from marine_web_view.s3_upload import S3Uploader

from rcl_interfaces.msg import ParameterDescriptor
from rcl_interfaces.msg import ParameterType

import rclpy
from rclpy.node import Node

# Same reasoning, same value as state_renderer's: not a per-PUT ceiling, just
# what stops one dead connection from occupying the upload worker forever.
UPLOAD_READ_TIMEOUT = 15

# How long `stop()` waits for the worker to finish the PUT it is inside. A
# daemon thread, so an unwinnable join costs a warning, not a hang.
UPLOAD_STOP_SECONDS = 5.0

# Index of the yaw (rotation about UP) variance in the row-major 6x6
# covariance AISContact carries. 5 * 6 + 5.
COV_YAW = 35

# Variance at or above which the yaw covariance is read as "no heading was
# reported". A decade below the tracker's `unknown_variance` stand-in (1e6),
# exactly as ais_layer.cpp's own threshold is, and for the same reason: the
# sentinel must still read as unknown if the two configurations drift, and no
# real AIS heading variance comes anywhere near this.
DEFAULT_HEADING_VARIANCE_THRESHOLD = 1.0e5

# One knot in metres per second.
KNOT = 0.514444

# Seconds between uploads, and the fallback an unusable `interval` falls back
# to. AIS does not need the position topic's 1 Hz.
DEFAULT_INTERVAL = 10.0

# ITU-R M.1371 navigational status 14: AIS-SART, MOB and EPIRB. A distress
# beacon, which in the ordinary case is a person in the water. Taken from the
# message rather than spelled as a literal, so the two cannot drift.
DISTRESS_NAV_STATUS = NavigationalStatus.NAVIGATIONAL_STATUS_SART_MOB_EPIRB

# The first three digits of the MMSIs ITU reserves for distress beacons: 970
# AIS-SART, 972 MOB, 974 EPIRB. This is the DEFINITIVE identifier, and the
# navigational status above is not: status 14 means "AIS-SART (active)", and
# NavigationalStatus.msg records in its own comments that status 15 --
# undefined, the default every silent contact carries -- is "also used by
# AIS-SART, MOB-AIS and EPIRB-AIS under test". So a beacon that is switched on
# but not yet transmitting 14 reaches the artifact on the status test alone.
# Of every filter here this is the one whose failure mode the operator named
# in person: somebody in the water, on a public CDN. Belt and braces.
DISTRESS_MMSI_PREFIXES = frozenset((970, 972, 974))

# Divisor that takes a 9-digit MMSI down to its first three digits.
MMSI_PREFIX_DIVISOR = 1000000

# Ship-and-cargo types 51 (search and rescue) and 55 (law enforcement). The
# page's own SPECIAL_TYPE table names them; this is the same two codes.
RESPONDER_SHIP_TYPES = frozenset((51, 55))

# ITU-R M.1371 message 9, the SAR aircraft position report. A SAR aircraft is
# a responder, and it is the one responder the ship-and-cargo-type test above
# CANNOT catch: message 9 carries no navigational status (ais_parser defaults
# it to 15) and an aircraft never sends a type 5 or 24 static report, so
# `ship_and_cargo_type` stays 0 forever -- the standard says the field is "not
# applicable to SAR aircraft". The message id is the only identifier there is,
# which is why ais_layer.cpp switches on this same field.
SAR_AIRCRAFT_MESSAGE_ID = 9

# Ceiling on each of the once-per-MMSI log records below. MMSI is an
# unauthenticated uint32 field on the air, so a set keyed on it is only as
# bounded as the traffic is honest -- and unlike `_contacts` these records
# have no timeout pruning them. Far above any real receiver's contact count
# over a shore watch: this is a runaway stop, not a working limit.
MAX_REMEMBERED_MMSI = 10000

# Below this speed the reported course is not recoverable. ais_parser encodes
# course over ground as the DIRECTION of the velocity vector (linear.x/y), so
# at a speed of zero both components are zero and the course is gone -- and
# atan2(0, 0) is 0.0, which would be published as a confident "heading east".
MIN_COURSE_SPEED = 0.01


# Unicode general categories that must not survive into the public popup.
# Cc/Cf are the C0 and C1 controls, DEL, and the format characters -- which
# include the bidi overrides (U+202A-E, U+2066-9) and the zero-width joiners.
# Those do not merely render badly: they REORDER neighbouring text, so a name
# can be dressed to read in the popup as something other than what it is. Cs
# is an unpaired surrogate: NOT the bare-NaN class of failure, because
# `json.dumps` defaults to ensure_ascii=True and escapes it to \ud800, which
# encodes cleanly and which JSON.parse accepts. It is dropped for the smaller
# reasons -- it is not a character, it renders as a replacement glyph, and it
# is one `ensure_ascii=False` away from a UnicodeEncodeError that would take
# out the whole artifact. Zl/Zp are the line and paragraph separators.
_UNPRINTABLE = frozenset(('Cc', 'Cf', 'Cs', 'Zl', 'Zp'))

# Ceiling on a published name or call sign. ITU gives the name 20 six-bit
# characters, plus up to 14 more for an aid-to-navigation extension, and the
# call sign 7; `Static.msg` declares both as UNBOUNDED ROS strings, so the
# length that actually arrives is whatever the decoder in front of this hands
# over -- from a transmitter nobody authenticates. `max_contacts` bounds the
# contact COUNT and says nothing about per-field size, and this text lands in
# the object every viewer downloads and in a popup title with no wrapping.
# 40 leaves headroom over the longest legitimate name; anything past it is
# not a name.
MAX_TEXT_CHARS = 40


def _clean(text):
    """Return a display string, or None if AIS said "not available".

    ITU-R M.1371 pads the name and call sign fields with '@'. Depending on the
    decoder those arrive either stripped to empty or still padded, and both
    mean the same thing: nothing has been received yet. The page renders None
    as "static info pending", which is a true statement about a contact seen
    by position report only.

    Names and call signs arrive over the air from anyone with a transmitter.
    The page already builds its popup out of text nodes, so this is not about
    markup -- it is that six-bit ASCII cannot carry a control or a bidi
    override but the decoder in front of this can hand one over anyway, and a
    public popup is not the right place to find out.
    Unprintables become spaces rather than vanishing, so nothing is silently
    glued into a different word, and runs of whitespace collapse. The result
    is capped at MAX_TEXT_CHARS: the ROS strings behind these are unbounded
    and `max_contacts` bounds only the contact count, so without this one
    transmitter decides how large the object every viewer downloads is.
    """
    if not text:
        return None
    scrubbed = ''.join(
        ' ' if unicodedata.category(char) in _UNPRINTABLE else char
        for char in str(text).replace('@', ' '))
    collapsed = ' '.join(scrubbed.split())
    if len(collapsed) > MAX_TEXT_CHARS:
        # Marked, not silently shortened: a truncated name read off a public
        # page should not look like the whole name.
        collapsed = collapsed[:MAX_TEXT_CHARS].rstrip() + '\u2026'
    return collapsed or None


def _remember(seen, mmsi):
    """Note an MMSI as already reported; True if it had not been.

    `seen` is a dict used as an insertion-ordered set, so the oldest record
    can be evicted once there are MAX_REMEMBERED_MMSI of them. What eviction
    costs is one repeated log line for a contact not seen in thousands of
    contacts, which is the right thing to lose: the alternative is a set that
    grows for the life of the node on input nobody authenticates.
    """
    if mmsi in seen:
        return False
    seen[mmsi] = None
    if len(seen) > MAX_REMEMBERED_MMSI:
        del seen[next(iter(seen))]
    return True


def _ignore_list(value):
    """Return the configured MMSIs as a frozenset, or refuse loudly.

    `ignore_mmsis` has to be declared with dynamic typing (see the
    declaration), so this is where its type is actually enforced. A
    misspelled list must not silently become an EMPTY ignore list: the
    failure mode is a vessel the operator asked to withhold being published
    anyway, which is the whole reason the parameter exists.
    """
    if not value:
        return frozenset()
    try:
        return frozenset(int(mmsi) for mmsi in value)
    except (TypeError, ValueError):
        raise ValueError(
            'ignore_mmsis must be a list of integer MMSIs, e.g. '
            '[366000001]; got {!r}'.format(value))


def excluded_from_public(contact):
    """Return why a contact must not be published, or None to publish it.

    Recorded operator decision (#357): distress beacons and SAR /
    law-enforcement contacts are kept off the public page. The reasoning is
    worth keeping next to the code rather than in a commit message.

    The page exists to show BizzyBoat in context, so it has no operational
    need for either category. AIS being public data elsewhere is not a reason
    for THIS page to rebroadcast a person in the water -- an AIS-SART is
    somebody's worst day, and a CDN-fronted map of it is a different act from
    a receiver hearing it. And the filter runs here, in the renderer, rather
    than on the page: hiding a marker in JavaScript still puts the position in
    the bucket, on the CDN and in the page source, one click away. The only
    version of this that keeps the data off a public network is the one that
    never uploads it.

    A responder's own position is withheld for the neighbouring reason: it
    reveals where a response is happening, and by inference what it is
    responding to, at the moment it is happening.

    Each category is tested on the identifier the standard actually gives it,
    not on the field that is most convenient. A distress beacon is identified
    by its reserved MMSI range as well as by its navigational status, because
    status 14 is "AIS-SART (ACTIVE)" and a beacon under test transmits the
    same 15 every silent contact defaults to. A SAR aircraft carries neither
    of those: message 9 has no navigational status and an aircraft sends no
    static report, so its message id is the whole of its identity.
    """
    if contact.navigational_status.status == DISTRESS_NAV_STATUS:
        return 'distress (AIS-SART / MOB / EPIRB)'
    if contact.id // MMSI_PREFIX_DIVISOR in DISTRESS_MMSI_PREFIXES:
        return 'distress beacon MMSI range (970 / 972 / 974)'
    if contact.static_info.ship_and_cargo_type in RESPONDER_SHIP_TYPES:
        return 'search and rescue / law enforcement'
    if contact.position_message_id == SAR_AIRCRAFT_MESSAGE_ID:
        return 'SAR aircraft (AIS message 9)'
    return None


def vessel_dimensions(static_info):
    """Return the hull block the page's hullShape() already understands.

    AIS reports four reference dimensions -- A/B/C/D, distance from the
    reported position to bow, stern, port and starboard. The page draws a hull
    from a length/width plus the offset of the reference point from the hull
    centre, which is the form marine_interfaces/PlatformList uses and which
    `hullShape()` is already written against. Converting here rather than
    sending AISContact.footprint means the page draws AIS contacts through the
    exact same, already-tested function it draws BizzyBoat with, instead of a
    second copy of the rotate-and-translate maths that turns a body-frame
    polygon into map coordinates.

    A = B = C = D = 0 is the standard's "dimensions not available", and it
    falls out of this as an all-zero block -- which `hullShape()` already
    handles by drawing its triangle or circle instead of a hull.

    A = C = 0 with B and D non-zero is a DIFFERENT encoding, spelled out in
    Static.msg: "reference point of reported position not available, but
    dimensions of ship are available". B is then the whole length and D the
    whole beam, and nothing at all is known about where on that hull the
    reported position sits. Read literally as offsets it says the position is
    at the bow, and the page would draw the entire vessel astern of its own
    marker -- a 100 m ship 100 m from where it is. Recorded decision (#357):
    the hull is CENTRED on the position in this case, which is the honest
    reading of "reference point not available" and the smallest-error
    placement of a hull whose reference point is unknown. This is a
    deliberate divergence from upstream `calculatePolygon`, which applies the
    offsets unconditionally; the tracker's consumers are planners that own
    their own margins, while this one is a picture a person reads.
    """
    bow = float(static_info.reference_to_bow_distance)
    stern = float(static_info.reference_to_stern_distance)
    port = float(static_info.reference_to_port_distance)
    starboard = float(static_info.reference_to_starboard_distance)
    if bow == 0.0 and stern != 0.0 and port == 0.0 and starboard != 0.0:
        # Dimensions without a reference point: B is the length, D the beam.
        return {
            'length': stern,
            'width': starboard,
            'reference_x': 0.0,
            'reference_y': 0.0,
        }
    length = bow + stern
    width = port + starboard
    # hullShape() computes toBow = length / 2 - reference_x, so the offset
    # that reproduces A is (B - A) / 2; likewise (D - C) / 2 across the beam.
    return {
        'length': length,
        'width': width,
        'reference_x': (stern - bow) / 2.0,
        'reference_y': (starboard - port) / 2.0,
    }


def heading_degrees(contact, variance_threshold):
    """Return the reported true heading in degrees, or None if none was.

    The quaternion alone cannot answer this: an unreported heading leaves
    either the identity quaternion (yaw 0, due east in ENU) or the null
    quaternion behind, and the first is indistinguishable from a vessel
    genuinely heading east. ais_contact_tracker folds "was a heading
    reported" into the yaw covariance, and that is what ais_layer.cpp tests
    (ais_layer.cpp:548-551); this mirrors it.
    """
    variance = contact.covariance[COV_YAW]
    if not (math.isfinite(variance) and 0.0 < variance < variance_threshold):
        return None
    q = contact.pose.orientation
    # The null quaternion ais_parser writes for an absent heading is not a
    # rotation at all, and normalising it is a division by zero.
    if q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w < 1e-6:
        return None
    heading = heading_from_quaternion(q)
    # Rounded like `speed_knots` and `course_deg` beside it, and for the same
    # reason: AIS transmits heading in whole degrees, the popup prints it to
    # zero decimal places, and full float precision would put seventeen
    # digits of quaternion arithmetic into an object every viewer downloads.
    return round(heading, 1) if math.isfinite(heading) else None


def speed_and_course(contact):
    """Return (speed in knots, course in degrees true), either may be None.

    ais_parser writes NaN into the velocity when the report carried no SOG or
    COG, so an absent velocity is missing rather than merely uncertain. It
    also encodes course as the direction of that vector, which means a
    stationary contact has no recoverable course -- reporting one anyway
    would publish `atan2(0, 0)`, i.e. a confident due east.
    """
    east = contact.twist.twist.linear.x
    north = contact.twist.twist.linear.y
    if not (math.isfinite(east) and math.isfinite(north)):
        return None, None
    speed = math.hypot(east, north)
    course = (None if speed < MIN_COURSE_SPEED
              else round(compass_degrees(math.atan2(north, east)), 1))
    return round(speed / KNOT, 1), course


class AisRenderer(Node):
    """Publish tracked AIS contacts as one GeoJSON artifact."""

    def __init__(self):
        """Declare parameters, subscribe to contacts, start the timer."""
        super().__init__('ais_renderer')

        self.declare_parameter('contacts_topic', '/ais/contacts')
        self.declare_parameter('bucket', 'unh-ccom-p11-live')
        self.declare_parameter('key', 'live/ais.geojson')
        self.declare_parameter('profile', 'p11-renderer')
        # AIS contacts do not need the position topic's 1 Hz, and every tick
        # that publishes is an S3 PUT (see the README's Cost section).
        self.declare_parameter('interval', DEFAULT_INTERVAL)
        self.declare_parameter('dry_run', False)
        self.declare_parameter('local_path', '/tmp/ais.geojson')
        # Generous on purpose. A shore receiver cannot tell a contact that
        # left from one that dropped below VHF range, and on a human-facing
        # display a marker flapping out and back on every reporting gap is
        # worse than a slightly stale one. Deliberately well above ais_layer's
        # own 90-180 s class-A/B costmap timeouts, which protect motion
        # planning and must react fast to a contact going stale.
        self.declare_parameter('contact_timeout', 600.0)
        # A ceiling on how many contacts are held and published at once.
        # PUT COUNT is contact-independent -- one object per interval either
        # way -- but object SIZE and the per-viewer CDN egress that follows it
        # are not, and MMSI is an unauthenticated uint32 on the air. Well
        # above what a shore receiver on the Piscataqua hears; reaching it
        # means an unusually busy watch, or traffic nobody transmitted.
        self.declare_parameter('max_contacts', 500)
        self.declare_parameter('heading_variance_threshold',
                               DEFAULT_HEADING_VARIANCE_THRESHOLD)
        # MMSIs never published, whatever they report. `ais_layer` carries the
        # same parameter on the same topic and its documented first purpose is
        # own ship: a shore receiver hears BizzyBoat's own transponder, and the
        # public page would then draw a second, grey, up-to-`interval`-lagged
        # hull beside the live one. It is also the only lever an operator has
        # to withhold a vessel the two standing categories do not cover, and
        # asking for one at the time is not a code change.
        #
        # `dynamic_typing` because an EMPTY list default carries no element
        # type: rclpy infers BYTE_ARRAY from `[]` and then rejects the
        # integer array an operator actually passes. The declared type below
        # is documentation; `_ignore_list` is what enforces it, and it names
        # the parameter when it refuses.
        self.declare_parameter(
            'ignore_mmsis', [],
            ParameterDescriptor(type=ParameterType.PARAMETER_INTEGER_ARRAY,
                                dynamic_typing=True))

        self.contacts_topic = self._param('contacts_topic')
        self.bucket = self._param('bucket')
        self.key = self._param('key')
        self.profile = self._param('profile')
        # Validated, not merely cast: `create_timer` raises on a
        # non-positive period, and it would raise HERE -- after the upload
        # worker below has been stood up but before `main` holds a node to
        # stop it. A typo on the parameter the launch file itself labels
        # "the cost lever" would take the renderer down and leave a thread
        # behind it. Same helper coverage_renderer validates its two with.
        self.interval, interval_ok = sane_interval(
            self._param('interval'), DEFAULT_INTERVAL)
        self.dry_run = bool(self._param('dry_run'))
        self.local_path = self._param('local_path')
        self.contact_timeout = float(self._param('contact_timeout'))
        self.max_contacts = max(1, int(self._param('max_contacts')))
        self.heading_variance_threshold = float(
            self._param('heading_variance_threshold'))
        self.ignore_mmsis = _ignore_list(self._param('ignore_mmsis'))

        if not interval_ok:
            self.get_logger().warn(
                'interval {} is not a usable period; using {:g} s'.format(
                    self._param('interval'), self.interval))

        self._contacts = {}
        self._writes = 0
        self._failures = 0
        self._skipped = 0
        self._last_written = None
        # MMSIs already reported as position-less, capped by `_remember`. A
        # shore receiver hears a silent AIS beacon every few seconds for as
        # long as it is switched on, so an unthrottled line here is a log
        # nobody can read -- and an unbounded record of who has been logged
        # is a leak with a transmitter on the other end of it.
        self._positionless = {}
        # One warning, ever, for the clock mismatch below: if it is wrong it
        # is wrong for every contact in the bag.
        self._clock_warned = False
        # MMSIs already reported as withheld, throttled and capped for the
        # same reasons `_positionless` is.
        self._withheld = {}

        # Same gate, same reasoning as the other two renderers: a dry run
        # writes to local_path and needs no AWS access at all, so no client is
        # built. `profile` is passed through UNCOALESCED, as state_renderer
        # does -- an operator who blanks it should fail loudly rather than
        # silently pick up whatever credentials the host carries.
        #
        # UPLOADS DO NOT RUN ON THIS THREAD: the payload goes to an
        # AsyncUploader, so a slow S3 endpoint cannot stop contact messages
        # being recorded.
        self._uploader = (None if self.dry_run else
                          S3Uploader(self.bucket, profile=self.profile,
                                     read_timeout=UPLOAD_READ_TIMEOUT))
        self._sender = (None if self.dry_run else
                        AsyncUploader(self._uploader,
                                      log_error=self._log_upload_failure))

        self.create_subscription(
            AISContact, self.contacts_topic, self._on_contact, 10)
        self.create_timer(self.interval, self._tick)

        target = (self.local_path if self.dry_run
                  else 's3://{}/{}'.format(self.bucket, self.key))
        self.get_logger().info(
            '{} -> {} every {}s, dropping contacts unheard for {:g}s{}'.format(
                self.contacts_topic, target, self.interval,
                self.contact_timeout,
                ' (dry run)' if self.dry_run else ''))
        if self.ignore_mmsis:
            self.get_logger().info(
                'never publishing MMSI {}'.format(
                    ', '.join(str(mmsi) for mmsi in sorted(self.ignore_mmsis))))

    def _param(self, name):
        """Return a declared parameter's value."""
        return self.get_parameter(name).value

    def _now(self):
        """Return the current time in seconds, on the header stamps' clock."""
        return self.get_clock().now().nanoseconds * 1e-9

    def _on_contact(self, msg):
        """Record the newest report for one MMSI.

        Newest wins by construction: the tracker republishes the whole
        accumulated contact -- position report plus whatever static and voyage
        data it has seen -- on every position report, so the latest message is
        the complete state of that contact.

        A contact with no usable position is refused here rather than filtered
        at render time, so nothing that cannot be drawn is ever held. So is a
        contact `excluded_from_public` or one the operator listed in
        `ignore_mmsis`: a withheld position must not be held in memory waiting
        for a code path that publishes it. The standing policy lives in
        `excluded_from_public`, which knows nothing of this node's
        configuration; the operator's own list is applied here.
        """
        reason = excluded_from_public(msg)
        if reason is None and msg.id in self.ignore_mmsis:
            reason = 'listed in ignore_mmsis'
        if reason is not None:
            # Dropped from memory too, not merely refused: a vessel can hoist
            # distress, or have its type resolve to 51/55 from a later type 5
            # message, long after it was first heard and published.
            self._contacts.pop(msg.id, None)
            self._positionless.pop(msg.id, None)
            if _remember(self._withheld, msg.id):
                # A withheld contact must not be indistinguishable from one
                # that was never heard: a quiet page has to be tellable from
                # a filtering one, and only this log can do that.
                self.get_logger().info(
                    'MMSI {} withheld from the public artifact -- {} '
                    '(logged once per contact)'.format(msg.id, reason))
            return
        self._withheld.pop(msg.id, None)
        position = msg.pose.position
        if not (math.isfinite(position.latitude) and
                math.isfinite(position.longitude)):
            if _remember(self._positionless, msg.id):
                self.get_logger().info(
                    'MMSI {} reports no position; not shown on the map '
                    '(logged once per contact)'.format(msg.id))
            return
        self._positionless.pop(msg.id, None)
        self._warn_if_already_expired(msg)
        if (msg.id not in self._contacts
                and len(self._contacts) >= self.max_contacts):
            self._evict_oldest(msg.id)
        self._contacts[msg.id] = msg

    def _evict_oldest(self, incoming):
        """Make room for a new MMSI at the ceiling; drop the oldest contact.

        The one closest to expiring anyway, so the ceiling costs the least
        interesting contact rather than an arbitrary one. Loud, and not
        throttled to silence: past this point the artifact is a TRUNCATED
        picture of the river, and a page that quietly shows some of the
        traffic is worse than one that shows all of it or says it cannot.
        """
        oldest = min(self._contacts,
                     key=lambda mmsi: self._stamp(self._contacts[mmsi]))
        del self._contacts[oldest]
        self.get_logger().warn(
            'at the max_contacts ceiling ({}); dropped MMSI {}, the oldest, '
            'to make room for MMSI {}. The published artifact is now a '
            'truncated picture of the traffic -- raise max_contacts if the '
            'river is genuinely this busy.'.format(
                self.max_contacts, oldest, incoming),
            throttle_duration_sec=60.0)

    def _warn_if_already_expired(self, contact):
        """Say so, once, if a contact arrives already past the timeout.

        This is the bag-replay mistake, made to announce itself. Contact age
        is a header stamp -- written when the shore receiver heard the
        contact -- subtracted from THIS NODE's clock, so a replay against a
        renderer left on the wall clock is handed stamps as old as the
        recording: `_expire` drops the whole set on the first tick and the
        artifact publishes `contacts: 0`. Nothing raises and the page is
        simply empty, which is indistinguishable from a quiet river.

        A live receiver cannot produce this: a contact that is already stale
        the moment it arrives means the two clocks disagree.
        """
        if self._clock_warned:
            return
        age = self._now() - self._stamp(contact)
        if age <= self.contact_timeout:
            return
        self._clock_warned = True
        self.get_logger().warn(
            'MMSI {} arrived already {:g}s old, past contact_timeout ({:g}s), '
            'so it expires on the very next tick. Replaying a bag? Play it '
            'with --clock and run this node with use_sim_time:=true. '
            'Otherwise this host and the receiver disagree about the time. '
            '(logged once)'.format(contact.id, round(age),
                                   self.contact_timeout))

    def _stamp(self, contact):
        """Return one contact's header stamp in seconds."""
        stamp = contact.header.stamp
        return stamp.sec + stamp.nanosec * 1e-9

    def _expire(self, now):
        """Drop contacts unheard from for longer than contact_timeout.

        Pruned from the dict, not merely excluded from the snapshot: memory
        stays bounded over a long shore watch, and an MMSI that ages out and
        later reappears is simply a fresh entry with no stale state behind it.
        """
        expired = [mmsi for mmsi, contact in self._contacts.items()
                   if now - self._stamp(contact) > self.contact_timeout]
        for mmsi in expired:
            del self._contacts[mmsi]
        return expired

    def _features(self):
        """Return one GeoJSON Feature per held contact, ordered by MMSI.

        Ordered so that the change detection in `_tick` compares like with
        like: dict order follows arrival, so an unordered list would look
        different on every tick that merely re-heard a contact.
        """
        features = []
        for mmsi in sorted(self._contacts):
            contact = self._contacts[mmsi]
            position = contact.pose.position
            stamp = self._stamp(contact)
            speed, course = speed_and_course(contact)
            features.append({
                'type': 'Feature',
                # GeoJSON orders coordinates [LONGITUDE, LATITUDE], the
                # reverse of Leaflet's L.marker([lat, lng]).
                'geometry': {
                    'type': 'Point',
                    'coordinates': [round(position.longitude, 6),
                                    round(position.latitude, 6)],
                },
                'properties': {
                    'role': 'ais',
                    'mmsi': int(mmsi),
                    # None until a type 5 or 24 message has been heard: a
                    # contact known by position alone is the ordinary case
                    # for the first minutes after it comes into range.
                    'name': _clean(contact.static_info.name),
                    'callsign': _clean(contact.static_info.callsign),
                    'ship_and_cargo_type': int(
                        contact.static_info.ship_and_cargo_type),
                    'navigational_status': int(
                        contact.navigational_status.status),
                    'speed_knots': speed,
                    'course_deg': course,
                    'heading_deg': heading_degrees(
                        contact, self.heading_variance_threshold),
                    'stamp': stamp,
                    'stamp_iso': time.strftime('%Y-%m-%dT%H:%M:%SZ',
                                               time.gmtime(stamp)),
                    'vessel': vessel_dimensions(contact.static_info),
                },
            })
        return features

    def _geojson(self, features):
        """Return the FeatureCollection payload for `features`.

        `allow_nan=False` is the backstop behind the position check in
        `_on_contact`: a NaN anywhere in this object would be serialised as a
        bare `NaN` token, which `JSON.parse` rejects -- so one bad contact
        would blank the AIS layer for every viewer. Failing here instead
        breaks one upload, in a process with a log, rather than the page.
        """
        return json.dumps({
            'type': 'FeatureCollection',
            'features': features,
            'properties': {
                'role': 'ais',
                'contacts': len(features),
                # When this artifact was last REBUILT, which is when the
                # contacts last changed -- not every tick. Publishing on an
                # unchanged set would pay an S3 PUT to say nothing happened
                # (see the README's Cost section), so an unchanging
                # `generated` means "nothing has changed since", not
                # necessarily "the renderer is alive". Liveness is each
                # feature's own `stamp`, which the page fades on.
                'generated': time.strftime('%Y-%m-%dT%H:%M:%SZ',
                                           time.gmtime()),
            },
        }, separators=(',', ':'), allow_nan=False)

    def _tick(self):
        """Run one publish pass, and never take the node down doing it.

        An exception out of a timer callback unwinds `rclpy.spin` and the
        process exits. `_geojson`'s docstring promises that a bad payload
        "breaks one upload, in a process with a log" -- that was only true of
        the payload, and only because nothing else in the pass could raise.
        The reachable one is an OSError from `write_atomic` on a dry run whose
        `local_path` directory does not exist or has filled up, and losing the
        renderer over a missing directory is not a trade anyone would pick on
        a shore watch. Contained, counted, and reported.
        """
        try:
            self._publish_pass()
        except Exception as exc:
            self._failures += 1
            self.get_logger().error(
                'publish pass failed, skipping this tick: {}: {}'.format(
                    type(exc).__name__, exc), throttle_duration_sec=30.0)

    def _publish_pass(self):
        """Publish the contact set if it has changed since the last write."""
        now = self._now()
        expired = self._expire(now)
        if expired:
            self.get_logger().info(
                'dropping {} contact(s) unheard for {:g}s: {}'.format(
                    len(expired), self.contact_timeout,
                    ', '.join(str(mmsi) for mmsi in sorted(expired))))

        features = self._features()
        # The signature is the features themselves, so any property a viewer
        # can see -- a position, a speed, a name that has just arrived --
        # publishes, and nothing else does.
        #
        # `stamp` IS such a property, deliberately: the page has no other
        # liveness signal (`generated` legitimately stops moving on a quiet
        # river) so it fades a contact whose stamp stops advancing. Leaving
        # the stamp out of this signature would therefore fade every live
        # contact that happens to be holding station. The cost is that a
        # contact merely re-heard still republishes -- one PUT per interval
        # while anything at all is in range, which is exactly the ceiling the
        # README's Cost section quotes. Hearing NOTHING is still free.
        #
        # Compared against what actually LANDED, not against what was handed
        # to the uploader: an accepted payload is not a published one.
        signature = json.dumps(features, separators=(',', ':'),
                               allow_nan=False, sort_keys=True)
        if signature == self._written_signature():
            self._skipped += 1
            self.get_logger().info(
                '{} contact(s) unchanged for {} ticks -- not uploading'.format(
                    len(features), self._skipped),
                throttle_duration_sec=60.0)
            return
        if self._skipped:
            self.get_logger().info(
                'contacts changed after {} idle ticks'.format(self._skipped))
            self._skipped = 0

        payload = self._geojson(features)
        if self.dry_run:
            write_atomic(self.local_path, payload)
            self._last_written = signature
            self._writes += 1
        else:
            self._queue(payload, signature)

    def _written_signature(self):
        """Return the signature of the artifact last CONFIRMED written.

        On a dry run that is the last atomic write; otherwise the last PUT the
        worker actually completed. Both are "what a viewer can see", which is
        what the change detection is asking about.
        """
        if self.dry_run:
            return self._last_written
        return self._sender.confirmed(self.key)

    def _queue(self, payload, signature):
        """Offer the artifact to the upload worker; never touches the network.

        Latest-wins per key (see AsyncUploader), so a slow endpoint costs
        superseded snapshots rather than a growing backlog of stale ones.
        Cache-Control drives CloudFront freshness; invalidation is
        deliberately not used, being billed per path.
        """
        accepted = self._sender.submit(
            payload.encode(), self.key, 'application/geo+json',
            'max-age={}'.format(max(1, int(self.interval))), tag=signature)
        if not accepted:
            reason = self._sender.dead()
            if reason is not None:
                self.get_logger().error(
                    'upload worker is dead ({}); {} is no longer reaching '
                    'S3, so the AIS layer is frozen at whatever it last '
                    'showed'.format(reason, self.key),
                    throttle_duration_sec=30.0)
            else:
                self.get_logger().error(
                    'upload worker refused {}: more keys in flight than slots'
                    .format(self.key), throttle_duration_sec=30.0)
        return accepted

    def upload_counts(self):
        """Return (writes, failures) across the local and S3 paths."""
        if self._sender is None:
            return self._writes, self._failures
        writes, failures, _ = self._sender.counts()
        return self._writes + writes, self._failures + failures

    def stop(self):
        """Stop the upload worker. Bounded, and never raises.

        Called from `main()`'s `finally`, so it must not be the reason
        `destroy_node()` is skipped.
        """
        if self._sender is None:
            return
        if not self._sender.stop(timeout=UPLOAD_STOP_SECONDS):
            reason = self._sender.dead()
            if reason is not None:
                self.get_logger().error(
                    'upload worker died before shutdown ({}); anything '
                    'published after that never reached S3'.format(reason))
            else:
                self.get_logger().warn(
                    'upload worker still inside a request after {:g} s; '
                    'abandoning it'.format(UPLOAD_STOP_SECONDS))

    def _log_upload_failure(self, key, exc):
        """Report one failed PUT. CALLED ON THE UPLOAD WORKER THREAD.

        Counted and logged, never raised: the next tick offers the snapshot
        again, because `confirmed` still reports the older signature.
        Throttled for the same reason state_renderer's is -- the failure that
        actually happens on a first deployment is a persistent one and it
        recurs every tick.
        """
        self.get_logger().error('upload of {} failed: {}'.format(
            key, describe_error(exc)), throttle_duration_sec=30.0)


def main(args=None):
    """Spin the AIS renderer until interrupted."""
    rclpy.init(args=args)
    node = None
    try:
        # Construction inside the try: the constructor builds the S3 client,
        # so a bad profile or region (ProfileNotFound, NoRegionError) or a
        # missing SDK would otherwise skip the finally entirely and leave
        # rclpy initialised.
        node = AisRenderer()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.stop()
            writes, failures = node.upload_counts()
            node.get_logger().info(
                'stopping: {} writes, {} failures, {} unchanged'.format(
                    writes, failures, node._skipped))
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
