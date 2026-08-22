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

import json
import math
import subprocess
import time

from geographic_msgs.msg import GeoPointStamped
from marine_interfaces.msg import PlatformList
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Imu
from sensor_msgs.msg import NavSatFix


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

        fix_type = (GeoPointStamped if self.msg_type == 'geopoint'
                    else NavSatFix)
        self.create_subscription(fix_type, self.topic, self._on_fix, 10)
        if self.ori_topic:
            self.create_subscription(Imu, self.ori_topic, self._on_imu, 10)
        self.create_subscription(
            PlatformList, self._param('platforms_topic'),
            self._on_platforms, 10)
        self.create_timer(self.interval, self._tick)

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
        """Record the newest position fix."""
        self._fix = msg

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
        wanted = (('position_topic', NavSatFix, self._on_fix),
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
        """Return the GeoJSON Feature for the current state."""
        latitude, longitude, altitude = self._lat_lon_alt(self._fix)
        stamp = self._fix_stamp()
        properties = {
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
                'coordinates': [longitude, latitude],
            },
            'properties': properties,
        }, separators=(',', ':'))

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
        if stamp == self._last_sent_stamp:
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

    def _write_local(self, payload, stamp):
        """Write the artifact to the local filesystem."""
        with open(self.local_path, 'w') as handle:
            handle.write(payload)
        self._last_sent_stamp = stamp
        self._writes += 1

    def _upload(self, payload, stamp):
        """Upload the artifact to S3.

        Cache-Control drives CloudFront freshness. Invalidation is deliberately
        not used: it is billed per path beyond a small monthly allowance, which
        at any real update rate would dwarf every other cost.
        """
        command = [
            'aws', 's3', 'cp', '-',
            's3://{}/{}'.format(self.bucket, self.key),
            '--content-type', 'application/geo+json',
            '--cache-control', 'max-age={}'.format(max(1, int(self.interval))),
            '--profile', self.profile,
        ]
        try:
            result = subprocess.run(command, input=payload.encode(),
                                    capture_output=True, timeout=20)
        except subprocess.TimeoutExpired:
            self._failures += 1
            self.get_logger().error('upload timed out')
            return
        if result.returncode != 0:
            self._failures += 1
            self.get_logger().error('upload failed: {}'.format(
                result.stderr.decode().strip()[:300]))
            return
        self._last_sent_stamp = stamp
        self._writes += 1


def main(args=None):
    """Spin the state renderer until interrupted."""
    rclpy.init(args=args)
    node = StateRenderer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.get_logger().info(
            'stopping: {} writes, {} failures, {} unchanged'.format(
                node._writes, node._failures, node._skipped))
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
