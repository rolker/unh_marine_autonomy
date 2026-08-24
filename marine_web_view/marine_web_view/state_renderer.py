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

"""Render live vessel state to static GeoJSON for the public web view.

Subscribes to a platform's navigation topics and writes a GeoJSON Feature --
position, heading and hull dimensions -- either to a local file or to an S3
object served through CloudFront. One-way by construction: no ROS surface is
exposed to the internet, and the page is static files that a viewer polls.

Self-configuring from marine_interfaces/PlatformList. The node is told a
platform NAME, not a set of topics: hull dimensions and the nav topic names
both come from the matching platform entry, so the same node serves any vessel
in the fleet unchanged. All of a platform's nav_sources are followed and the
newest message wins, matching CAMP's Platform::shape().

Uploads happen only on NEW data -- the fix stamp must have advanced. An idle or
disconnected vessel therefore costs nothing, and the page derives staleness
from the stamp it last saw rather than from upload cadence.

See rolker/unh_marine_autonomy#341 and #333.
"""

from collections import deque
import json
import math
import os
import time

from geographic_msgs.msg import GeoPointStamped
from marine_interfaces.msg import PlatformList
from marine_web_view.s3_upload import AsyncUploader
from marine_web_view.s3_upload import describe_error
from marine_web_view.s3_upload import S3Uploader
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Imu
from sensor_msgs.msg import NavSatFix

# Track decimation ------------------------------------------------------------
#
# Older parts of the track are simplified rather than dropped, so the rough path
# survives while the artifact stays small. Douglas-Peucker is used because it is
# shape-preserving: it keeps the vertices where the path actually turns and
# collapses straight runs to their endpoints. Uniform "every Nth point"
# decimation does the opposite -- it rounds off corners, which for lawnmower
# survey lines is exactly the information worth keeping.
#
# Bands are (max_age_seconds, tolerance_metres). The newest band has tolerance
# 0.0, meaning every fix is kept. The oldest band is a catch-all (max_age inf)
# so the whole retained history renders however far back track_seconds keeps it
# -- a finite ceiling here would silently drop everything older than it, so a
# track_seconds larger than the ceiling would never fully render.
# How long the UPLOAD WORKER waits for a response that has stopped arriving.
# Not a per-PUT ceiling and not a bound on anything the executor thread does
# -- see `_boto3_client`, and the construction of `_sender` below. Its only
# job is to stop one dead connection from occupying the worker forever, so a
# later position can be sent.
UPLOAD_READ_TIMEOUT = 15

# How long `stop()` waits for the worker to finish the PUT it is inside. It
# is a daemon thread, so an unwinnable join costs a warning, not a hang.
UPLOAD_STOP_SECONDS = 5.0

TRACK_BANDS = (
    (120.0, 0.0),           # last 2 min: full resolution
    (900.0, 3.0),           # to 15 min: gentle
    (3600.0, 10.0),         # to 1 hour
    (float('inf'), 30.0),   # older than 1 hour: rough path only
)


def _to_local(points, lat0):
    """Project lat/lon to local metres about lat0 (equirectangular).

    Accurate to well under a metre over the tens of kilometres a track spans,
    and it keeps the tolerances above in real metres rather than degrees.
    """
    scale = math.cos(math.radians(lat0))
    return [(lon * 111319.49 * scale, lat * 111319.49) for _, lat, lon in points]


def _rdp_mask(xy, tolerance):
    """Return a keep-mask for Douglas-Peucker simplification of xy."""
    n = len(xy)
    keep = [False] * n
    if n == 0:
        return keep
    keep[0] = keep[n - 1] = True
    stack = [(0, n - 1)]
    while stack:
        first, last = stack.pop()
        if last <= first + 1:
            continue
        ax, ay = xy[first]
        bx, by = xy[last]
        dx, dy = bx - ax, by - ay
        span = math.hypot(dx, dy)
        worst, worst_i = -1.0, -1
        for i in range(first + 1, last):
            px, py = xy[i]
            if span == 0.0:
                dist = math.hypot(px - ax, py - ay)
            else:
                # perpendicular distance from the chord
                dist = abs(dy * px - dx * py + bx * ay - by * ax) / span
            if dist > worst:
                worst, worst_i = dist, i
        if worst > tolerance:
            keep[worst_i] = True
            stack.append((first, worst_i))
            stack.append((worst_i, last))
    return keep


def decimate_track(points, now):
    """Simplify a track, progressively more so with age.

    points is a sequence of (stamp, lat, lon), oldest first. Each age band is
    simplified independently at its own tolerance and the results concatenated,
    so a band boundary never drops a point that anchors the newer segment.
    """
    if len(points) < 3:
        return list(points)
    out = []
    previous_age = -1.0
    for max_age, tolerance in TRACK_BANDS:
        band = [p for p in points
                if previous_age < (now - p[0]) <= max_age]
        previous_age = max_age
        if not band:
            continue
        if tolerance <= 0.0 or len(band) < 3:
            out.extend(band)
            continue
        mask = _rdp_mask(_to_local(band, band[0][1]), tolerance)
        out.extend(p for p, k in zip(band, mask) if k)
    out.sort(key=lambda p: p[0])
    return out


class StateRenderer(Node):
    """Publish a vessel's live position and heading as a GeoJSON artifact."""

    def __init__(self):
        """Declare parameters and start subscriptions and the upload timer."""
        super().__init__('state_renderer')

        self.declare_parameter('topic', '/ben/sensors/nav/position')
        self.declare_parameter('msg_type', 'navsatfix')
        self.declare_parameter('orientation_topic', '')
        self.declare_parameter('platforms_topic', '/marine/platforms')
        self.declare_parameter('platform_name', '')
        self.declare_parameter('bucket', 'unh-ccom-p11-live')
        self.declare_parameter('key', 'live/position.geojson')
        self.declare_parameter('profile', 'p11-renderer')
        self.declare_parameter('interval', 1.0)
        self.declare_parameter('dry_run', False)
        self.declare_parameter('local_path', '/tmp/position.geojson')
        self.declare_parameter('track_key', 'live/track.geojson')
        self.declare_parameter('track_local_path', '/tmp/track.geojson')
        self.declare_parameter('track_seconds', 14400.0)
        self.declare_parameter('track_max_points', 1200)
        # The track is published on its OWN, slower timer. Old track does not
        # change, so re-sending it at the position rate is waste: at 1 Hz a
        # ~14 kB track is ~1.2 GB/day per open viewer against ~34 MB for the
        # point alone.
        self.declare_parameter('track_interval', 30.0)
        # Fallback hull, used only until the first PlatformList arrives.
        # PlatformList is VOLATILE rather than latched, so that can take a
        # few seconds after start-up.
        self.declare_parameter('vessel_length', 4.25)
        self.declare_parameter('vessel_width', 1.7)
        self.declare_parameter('reference_x', 0.525)
        self.declare_parameter('reference_y', 0.5)

        self.topic = self._param('topic')
        self.msg_type = str(self._param('msg_type')).lower()
        self.ori_topic = self._param('orientation_topic')
        self.bucket = self._param('bucket')
        self.key = self._param('key')
        self.profile = self._param('profile')
        self.interval = float(self._param('interval'))
        self.dry_run = bool(self._param('dry_run'))
        self.local_path = self._param('local_path')
        self.track_key = self._param('track_key')
        self.track_local_path = self._param('track_local_path')
        self.track_seconds = float(self._param('track_seconds'))
        self.track_max_points = int(self._param('track_max_points'))
        self.track_interval = float(self._param('track_interval'))
        self.platform_name = (self._param('platform_name')
                              or self.topic.strip('/').split('/')[0])
        self.vessel = {
            'length': float(self._param('vessel_length')),
            'width': float(self._param('vessel_width')),
            'reference_x': float(self._param('reference_x')),
            'reference_y': float(self._param('reference_y')),
            'name': None,
            'source': 'parameter fallback',
        }

        self._fix = None
        self._imu = None
        self._writes = 0
        self._failures = 0
        self._skipped = 0
        self._last_sent_stamp = None
        self._dyn_subs = {}
        # deque, not list: the window is trimmed from the left on every fix
        # (below), which is O(1) with popleft() but an O(n) rebuild with a list
        # comprehension -- and at 5 Hz nav over a 4 h window that is ~72k tuples
        # rebuilt several times a second inside the subscriber callback.
        self._history = deque()
        self._track_stamp = None

        # Not on a dry run: writing to local_path needs no AWS access at all,
        # and constructing a client would go looking for a profile that a
        # simulator host has no reason to have. `self.profile` is passed
        # through UNCOALESCED -- this node has always required a profile, and
        # an operator who blanks it should still fail loudly rather than
        # silently pick up whatever credentials the host happens to carry.
        # (coverage_renderer deliberately differs: see its own construction.)
        #
        # UPLOADS DO NOT RUN ON THIS THREAD. _tick and _track_tick are timer
        # callbacks on rclpy.spin's single-threaded executor, the same thread
        # _on_fix runs on, so a PUT issued here stops position fixes being
        # recorded for as long as it takes -- and they are not replayed, so
        # the track keeps a permanent hole for the stall. No timeout value
        # makes that acceptable and no exact one exists to choose (see
        # _boto3_client): the payloads go to an AsyncUploader instead, which
        # keeps only the newest object per key. read_timeout still caps the
        # WORKER's wait on a response that has stopped arriving; it is not a
        # bound on anything the executor thread does.
        self._uploader = (None if self.dry_run else
                          S3Uploader(self.bucket, profile=self.profile,
                                     read_timeout=UPLOAD_READ_TIMEOUT))
        self._sender = (None if self.dry_run else
                        AsyncUploader(self._uploader,
                                      log_error=self._log_upload_failure))

        self.fix_type = (GeoPointStamped if self.msg_type == 'geopoint'
                         else NavSatFix)
        self.create_subscription(self.fix_type, self.topic, self._on_fix, 10)
        if self.ori_topic:
            self.create_subscription(Imu, self.ori_topic, self._on_imu, 10)
        self.create_subscription(
            PlatformList, self._param('platforms_topic'),
            self._on_platforms, 10)
        self.create_timer(self.interval, self._tick)
        self.create_timer(self.track_interval, self._track_tick)

        target = (self.local_path if self.dry_run
                  else 's3://{}/{}'.format(self.bucket, self.key))
        self.get_logger().info(
            "platform '{}' -> {} every {}s{}".format(
                self.platform_name, target, self.interval,
                ' (dry run)' if self.dry_run else ''))

    def _param(self, name):
        """Return a declared parameter's value."""
        return self.get_parameter(name).value

    def _on_fix(self, msg):
        """Record the newest position fix and append it to the track.

        History is accumulated at the full message rate, independently of the
        upload interval: the track a viewer sees is where the vessel went, not
        an artifact of when anyone happened to poll.
        """
        latitude, longitude, _ = self._lat_lon_alt(msg)
        stamp = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9
        # Newest wins: with several position subscriptions an out-of-order fix
        # can arrive after a newer one. Reject it BEFORE it touches self._fix,
        # or the published point would jump backward in time -- the opposite of
        # the "newest message wins" contract this node is built on.
        if self._history and stamp <= self._history[-1][0]:
            return
        self._fix = msg
        self._history.append((stamp, latitude, longitude))
        cutoff = stamp - self.track_seconds
        while self._history and self._history[0][0] < cutoff:
            self._history.popleft()

    def _on_imu(self, msg):
        """Record the newest orientation."""
        self._imu = msg

    def _on_platforms(self, msg):
        """Adopt hull dimensions and nav topics from the matching platform."""
        for platform in msg.platforms:
            namespace = platform.platform_namespace or platform.name
            if (namespace != self.platform_name
                    and platform.name != self.platform_name):
                continue
            vessel = {
                'length': float(platform.length),
                'width': float(platform.width),
                'reference_x': float(platform.reference_x),
                'reference_y': float(platform.reference_y),
                'name': platform.name,
                'source': 'PlatformList',
            }
            if vessel != self.vessel:
                self.get_logger().info(
                    'hull from PlatformList: {} {:.2f} x {:.2f} m'.format(
                        platform.name, vessel['length'], vessel['width']))
            self.vessel = vessel
            self._subscribe_nav_sources(platform, namespace)
            return

    def _subscribe_nav_sources(self, platform, namespace):
        """Subscribe to whatever nav topics the platform advertises.

        Topics in nav_sources are relative to the platform namespace. Every
        source is followed rather than only the highest priority one: CAMP uses
        whichever has the newest message, and so does this node.
        """
        # Discovered position topics carry the same message type the node was
        # configured for (msg_type) -- subscribing them as NavSatFix regardless
        # would hand _on_fix a GeoPointStamped it cannot read.
        wanted = (('position_topic', self.fix_type, self._on_fix),
                  ('orientation_topic', Imu, self._on_imu))
        for source in getattr(platform, 'nav_sources', []):
            for attribute, msg_type, callback in wanted:
                relative = getattr(source, attribute, '')
                if not relative:
                    continue
                topic = (relative if relative.startswith('/')
                         else '/{}/{}'.format(namespace, relative))
                if topic in self._dyn_subs:
                    continue
                self._dyn_subs[topic] = self.create_subscription(
                    msg_type, topic, callback, 10)
                self.get_logger().info(
                    "nav source '{}' (priority {}): subscribing {}".format(
                        source.name, source.priority, topic))

    def _lat_lon_alt(self, msg):
        """Return (latitude, longitude, altitude) for either fix type."""
        if self.msg_type == 'geopoint':
            return (msg.position.latitude, msg.position.longitude,
                    msg.position.altitude)
        return msg.latitude, msg.longitude, msg.altitude

    def _heading_deg(self):
        """Return compass heading in degrees true, or None without an IMU.

        ROS uses ENU (REP-103), so yaw is measured counter-clockwise from EAST
        while a compass heading is clockwise from NORTH -- hence 90 - yaw.
        Verified in simulation against course made good.
        """
        if self._imu is None:
            return None
        q = self._imu.orientation
        yaw = math.atan2(2.0 * (q.w * q.z + q.x * q.y),
                         1.0 - 2.0 * (q.y * q.y + q.z * q.z))
        return (90.0 - math.degrees(yaw)) % 360.0

    def _fix_stamp(self):
        """Return the current fix's stamp in seconds."""
        stamp = self._fix.header.stamp
        return stamp.sec + stamp.nanosec * 1e-9

    def _geojson(self):
        """Return the vessel-position Feature.

        Deliberately just the current point. The track is a separate, larger
        object on a slower timer -- see _track_geojson.
        """
        latitude, longitude, altitude = self._lat_lon_alt(self._fix)
        stamp = self._fix_stamp()
        properties = {
            'role': 'vessel',
            'heading': self._heading_deg(),
            'altitude': altitude,
            'frame_id': self._fix.header.frame_id,
            'stamp': stamp,
            'stamp_iso': time.strftime('%Y-%m-%dT%H:%M:%SZ',
                                       time.gmtime(stamp)),
            'generated': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            'vessel': self.vessel,
        }
        if self.msg_type != 'geopoint':
            properties['fix_status'] = int(self._fix.status.status)
        return json.dumps({
            'type': 'Feature',
            # GeoJSON orders coordinates [LONGITUDE, LATITUDE], the reverse of
            # Leaflet's L.marker([lat, lng]).
            'geometry': {
                'type': 'Point',
                'coordinates': [round(longitude, 6), round(latitude, 6)],
            },
            'properties': properties,
        }, separators=(',', ':'))

    def _track_geojson(self, stamp):
        """Return the decimated track as a LineString Feature."""
        track = decimate_track(self._history, stamp)
        if len(track) > self.track_max_points:
            # Safety net only. Trimming the oldest beats unbounded growth; if
            # this fires routinely the bands want widening, not the cap
            # lowering -- the point of the bands is to keep the whole path.
            self.get_logger().warn(
                'track hit the {} point cap; oldest fixes trimmed'.format(
                    self.track_max_points))
            track = track[-self.track_max_points:]
        if len(track) < 2:
            return None
        return json.dumps({
            'type': 'Feature',
            'geometry': {
                'type': 'LineString',
                'coordinates': [[round(lon, 6), round(lat, 6)]
                                for _, lat, lon in track],
            },
            'properties': {
                'role': 'track',
                'points': len(track),
                'raw_points': len(self._history),
                'span_seconds': round(stamp - track[0][0], 1),
                # Where this track ends, so a viewer can bridge the gap between
                # the last published point and the vessel's current position
                # with the fixes it has seen since. Without it the trail lags
                # by up to track_interval.
                'start_stamp': track[0][0],
                'end_stamp': track[-1][0],
                'generated': time.strftime('%Y-%m-%dT%H:%M:%SZ',
                                           time.gmtime()),
            },
        }, separators=(',', ':'))

    def _track_tick(self):
        """Publish the track if it has grown since the last publication.

        Compared against what actually LANDED, not against what was handed to
        the uploader: an accepted payload is not a published one, and a track
        whose upload failed has to be offered again on the next tick.
        """
        if not self._history:
            return
        stamp = self._history[-1][0]
        if stamp == self._sent_stamp(self.track_key):
            return
        payload = self._track_geojson(stamp)
        if payload is None:
            return
        if self.dry_run:
            self._write_atomic(self.track_local_path, payload)
            self._track_stamp = stamp
        else:
            self._queue(payload, self.track_key, self.track_interval, stamp)

    def _tick(self):
        """Write the current state if it has changed since the last write."""
        if self._fix is None:
            self.get_logger().warn('no position yet',
                                   throttle_duration_sec=15.0)
            return

        # Publish only genuinely new data. Without this the last fix would be
        # re-sent every tick after the vessel goes quiet -- paying per S3 PUT
        # to report that nothing changed, and masking nothing, because the
        # page derives staleness from the stamp.
        stamp = self._fix_stamp()
        if stamp == self._sent_stamp(self.key):
            self._skipped += 1
            self.get_logger().warn(
                'position unchanged for {} ticks -- not uploading'.format(
                    self._skipped),
                throttle_duration_sec=30.0)
            return
        if self._skipped:
            self.get_logger().info(
                'data resumed after {} idle ticks'.format(self._skipped))
            self._skipped = 0

        payload = self._geojson()
        if self.dry_run:
            self._write_local(payload, stamp)
        else:
            self._upload(payload, stamp)

    @staticmethod
    def _write_atomic(path, payload):
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

    def _write_local(self, payload, stamp):
        """Write the artifact to the local filesystem."""
        self._write_atomic(self.local_path, payload)
        self._last_sent_stamp = stamp
        self._writes += 1

    def _upload(self, payload, stamp):
        """Hand the position artifact to the upload worker."""
        self._queue(payload, self.key, self.interval, stamp)

    def _sent_stamp(self, key):
        """Return the stamp of the artifact last CONFIRMED written.

        On a dry run that is the last atomic write; otherwise it is the last
        PUT the worker actually completed. Both are "what a viewer can see",
        which is what the change detection in the two ticks is asking about.
        """
        if self.dry_run:
            return (self._last_sent_stamp if key == self.key
                    else self._track_stamp)
        return self._sender.confirmed(key)

    def _queue(self, payload, key, max_age, stamp):
        """Offer one object to the upload worker; never touches the network.

        Returns True if it was accepted. Latest-wins: a payload still pending
        for this key is REPLACED, so a slow endpoint costs superseded
        positions rather than a growing backlog of stale ones (see
        AsyncUploader). Nothing here can block the executor thread, so a
        stalled upload no longer stops fixes being recorded.

        Cache-Control drives CloudFront freshness. Invalidation is deliberately
        not used: it is billed per path beyond a small monthly allowance, which
        at any real update rate would dwarf every other cost.
        """
        accepted = self._sender.submit(
            payload.encode(), key, 'application/geo+json',
            'max-age={}'.format(max(1, int(max_age))), tag=stamp)
        if not accepted:
            # Two reasons, and they are not the same news. A dead worker is
            # the one that has to reach the operator: nothing published from
            # here on will ever be sent, so the map is frozen at whatever it
            # last showed, and this is the only place that says so every tick.
            # The other is a bound reasoned wrongly -- this node publishes two
            # keys and the worker has four slots.
            reason = self._sender.dead()
            if reason is not None:
                self.get_logger().error(
                    'upload worker is dead ({}); {} and everything else this '
                    'node publishes is no longer reaching S3'.format(
                        reason, key), throttle_duration_sec=30.0)
            else:
                self.get_logger().error(
                    'upload worker refused {}: more keys in flight than slots'
                    .format(key), throttle_duration_sec=30.0)
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
                # It was not slow, it was gone -- possibly for most of the
                # run. The counts printed just below cannot show that on
                # their own, and this is the last chance to say it.
                self.get_logger().error(
                    'upload worker died before shutdown ({}); anything '
                    'published after that never reached S3'.format(reason))
            else:
                self.get_logger().warn(
                    'upload worker still inside a request after {:g} s; '
                    'abandoning it'.format(UPLOAD_STOP_SECONDS))

    def _log_upload_failure(self, key, exc):
        """Report one failed PUT. CALLED ON THE UPLOAD WORKER THREAD.

        A failure is counted (by the worker) and logged, never raised: the
        next tick offers the object again, because `confirmed` still reports
        the older stamp. Same behaviour the CLI shell-out this replaced had.
        """
        self.get_logger().error('upload of {} failed: {}'.format(
            key, describe_error(exc)))


def main(args=None):
    """Spin the state renderer until interrupted."""
    rclpy.init(args=args)
    node = StateRenderer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
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
