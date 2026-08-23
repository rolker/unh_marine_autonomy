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

"""Render live sonar coverage to static web-map tiles.

Consumes the ADR-0008 display transport -- catalog, request, tile -- keeps a
GGGS tile cache reconciled against the source, and renders the covered area as
slippy-map PNGs written beside the position artifacts.

Read-only against the display transport: this node never writes back to the
durable stores. The transport is a lossy display projection that is always
rebuildable from them (ADR-0008), so nothing here is a system of record.

The protocol, and why each part matters:

* The source publishes a **complete** catalog snapshot. We reconcile our cache
  against it, request what we are missing or stale on, and prune what it no
  longer holds. Completeness is a precondition -- prune-on-absence against a
  partial catalog would read as "the source dropped everything I cannot see".
* Tiles arrive as **dirty sub-windows**, not whole tiles, patched in at their
  offset.
* Live push is **best-effort**; a lost patch is healed by the next catalog
  round, which re-requests the tile in full.

Colour follows #342's basemap ramp over a fixed 0-40 m scale, so coverage and
bathymetry read on one scale, with a small deliberate offset so the layers stay
distinguishable where they overlap. See the plan's ADR-0001 note: this is an
interim deviation until marine_colormap gains a Python binding (#137).

The band's values are z in the MAP frame, not depth below chart datum, so they
are referenced through TF before they are coloured -- see _update_datum_offset.
"""

import io
import os
import subprocess
import tempfile
import threading

from marine_interfaces.msg import SonarVisualizationTile
from marine_interfaces.msg import TileCatalog
from marine_interfaces.msg import TileIndex
from marine_interfaces.msg import TileRequest

from marine_web_view import gggs
from marine_web_view import tiles as tile_util
from marine_web_view.reconciler import is_valid_index
from marine_web_view.reconciler import TileCatalogReconciler

import numpy

from PIL import Image

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSDurabilityPolicy
from rclpy.qos import QoSProfile
from rclpy.qos import QoSReliabilityPolicy
from rclpy.time import Time

import tf2_ros

# Depth ramp shared with the basemap (#342), extracted from CCOM's published
# BTY_4m_HighRes_BlueGreen_DRA service. Deep first, surface last.
RAMP = (
    (0, 38, 115), (1, 40, 116), (6, 57, 125), (11, 68, 130),
    (16, 81, 137), (20, 92, 142), (21, 94, 143), (31, 116, 153),
    (41, 136, 162), (51, 155, 171), (62, 175, 180), (74, 189, 183),
    (88, 196, 178), (104, 204, 175), (107, 205, 175), (118, 211, 176),
    (127, 215, 176), (138, 219, 175), (150, 224, 177), (162, 229, 181),
    (174, 234, 186), (168, 232, 184), (182, 237, 190), (197, 243, 199),
)
MAX_DEPTH = 40.0

# Default vertical-reference frames. The band carries z in the MAP frame --
# ellipsoidal, and nothing like a depth below chart datum -- so it has to be
# referenced before it can be coloured. See _update_datum_offset.
DEFAULT_MAP_FRAME = 'ben/map'
DEFAULT_CHART_DATUM_FRAME = 'ben/chart_datum'

# Metres of tide change that force a re-render. Same lever, same reasoning as
# s57_layer's tide_invalidate_threshold: small enough that the colours track
# the tide, large enough that a centimetre of TF jitter does not re-render and
# re-upload the whole mosaic. One 8-bit step of the 0-40 m ramp is 0.16 m.
DEFAULT_TIDE_INVALIDATE_THRESHOLD = 0.15

# Coverage is composited over the basemap, so an identical palette would make
# it invisible. A small offset keeps the two comparable but distinguishable.
COVERAGE_TINT = (1.06, 0.97, 0.88)

# Wire messages are external input and both dimension fields are uint16, so a
# corrupt or hostile tile can ask for 65535 x 65535 float32 cells -- about
# 17 GB -- in a single allocation on the executor thread, which is a node
# death rather than a dropped message. Validate BEFORE allocating anything,
# with the same shape of guard CAMP added after the 2026-07-23 operator-station
# crash (sonar_live_cache_layer.cpp:436, kMaxImageEdge = 4096). The producer
# emits 960 x 960 grids (gggs cell_rows_per_grid), so 4096 leaves generous room
# without admitting anything absurd; raising producer tile size means raising
# this.
MAX_TILE_EDGE = 4096
MAX_TILE_BYTES = 256 * 1024 * 1024

# A GGGS grid at a coarse level covers an enormous area: level 0 is 8 deg on a
# side, which at zoom 15 is ~1.6e7 slippy tiles. Enumerating that into the
# dirty set from one message hangs the node and would then try to upload it.
# A level-10 grid is ~50 tiles at zoom 15, so this bounds the sane case by
# orders of magnitude while making the insane one a logged rejection.
MAX_DIRTY_TILES_PER_GRID = 4096

# One fully transparent 256x256 PNG, used to un-publish a slippy tile whose
# coverage has been pruned. Built once: it is a couple of hundred bytes.
_EMPTY_PNG = None


def empty_png():
    """Return the bytes of a fully transparent 256x256 PNG."""
    global _EMPTY_PNG
    if _EMPTY_PNG is None:
        buffer = io.BytesIO()
        Image.new('RGBA', (256, 256), (0, 0, 0, 0)).save(
            buffer, format='PNG', optimize=True)
        _EMPTY_PNG = buffer.getvalue()
    return _EMPTY_PNG


def ramp_colour(fraction):
    """Return an (r, g, b) tuple; fraction 0 is deepest, 1 the surface."""
    position = max(0.0, min(1.0, fraction)) * (len(RAMP) - 1)
    low = int(position)
    high = min(low + 1, len(RAMP) - 1)
    blend = position - low
    return tuple(
        int(round(RAMP[low][i] * (1.0 - blend) + RAMP[high][i] * blend))
        for i in range(3))


def colour_table():
    """Return a 256-entry RGB lookup for depths 0..MAX_DEPTH, tinted."""
    table = numpy.zeros((256, 3), dtype=numpy.uint8)
    for i in range(256):
        red, green, blue = ramp_colour(1.0 - i / 255.0)
        table[i] = [
            min(255, int(round(red * COVERAGE_TINT[0]))),
            min(255, int(round(green * COVERAGE_TINT[1]))),
            min(255, int(round(blue * COVERAGE_TINT[2]))),
        ]
    return table


class CoverageRenderer(Node):
    """Reconcile a coverage tile cache and render it to web-map PNGs."""

    def __init__(self):
        """Declare parameters, subscribe to the transport, start the timers."""
        super().__init__('coverage_renderer')

        self.declare_parameter('coverage_namespace',
                               '/ben/sensors/mbes/cube_bathymetry')
        self.declare_parameter('band', 'depth')
        self.declare_parameter('zoom', 15)
        self.declare_parameter('bucket', 'unh-ccom-p11-live')
        self.declare_parameter('prefix', 'live/coverage')
        self.declare_parameter('profile', 'p11-renderer')
        self.declare_parameter('render_interval', 20.0)
        self.declare_parameter('request_interval', 5.0)
        self.declare_parameter('dry_run', False)
        self.declare_parameter('local_dir', '/tmp/coverage')
        self.declare_parameter('cache_control', 60)
        self.declare_parameter('map_frame', DEFAULT_MAP_FRAME)
        self.declare_parameter('chart_datum_frame', DEFAULT_CHART_DATUM_FRAME)
        self.declare_parameter('tide_invalidate_threshold',
                               DEFAULT_TIDE_INVALIDATE_THRESHOLD)

        self.band_name = self._param('band')
        self.zoom = int(self._param('zoom'))
        self.bucket = self._param('bucket')
        self.prefix = str(self._param('prefix')).strip('/')
        self.profile = self._param('profile')
        self.dry_run = bool(self._param('dry_run'))
        self.local_dir = self._param('local_dir')
        self.cache_control = int(self._param('cache_control'))
        self.map_frame = str(self._param('map_frame')).strip()
        self.chart_datum_frame = str(self._param('chart_datum_frame')).strip()
        self.tide_invalidate_threshold = float(
            self._param('tide_invalidate_threshold'))
        # A ROS parameter is external input. Negative or non-finite would make
        # every TF jitter exceed the threshold and re-render the whole mosaic
        # on every pass -- the same validation s57_layer applies to its own
        # copy of this lever.
        if not (self.tide_invalidate_threshold >= 0.0
                and self.tide_invalidate_threshold < float('inf')):
            self.get_logger().warn(
                'invalid tide_invalidate_threshold {}; using {} m'.format(
                    self.tide_invalidate_threshold,
                    DEFAULT_TIDE_INVALIDATE_THRESHOLD))
            self.tide_invalidate_threshold = DEFAULT_TIDE_INVALIDATE_THRESHOLD

        self._reconciler = TileCatalogReconciler()
        self._tiles = {}          # index -> full-tile float array
        self._applied = {}        # index -> newest patch version applied
        self._dirty = set()       # slippy tiles needing a re-render
        self._lock = threading.Lock()
        self._colours = colour_table()
        self._pending_request = []
        self._published = set()   # slippy tiles currently holding coverage
        self._rendered = 0
        self._failures = 0
        self._datum_offset = None

        # Only stand up TF when the correction is actually configured: an
        # empty chart_datum_frame is the documented way to say "the band is
        # already referenced", and it must not then warn about a TF nobody
        # publishes.
        self._tf_buffer = None
        self._tf_listener = None
        if self.chart_datum_frame and self.map_frame:
            self._tf_buffer = tf2_ros.Buffer()
            self._tf_listener = tf2_ros.TransformListener(
                self._tf_buffer, self, spin_thread=False)
        else:
            self._datum_offset = 0.0
            self.get_logger().warn(
                'chart-datum correction disabled: the depth band will be '
                'coloured as if it were already referenced to chart datum')

        namespace = str(self._param('coverage_namespace')).rstrip('/')
        latched = QoSProfile(depth=1)
        latched.durability = QoSDurabilityPolicy.TRANSIENT_LOCAL
        latched.reliability = QoSReliabilityPolicy.RELIABLE
        best_effort = QoSProfile(depth=50)
        best_effort.reliability = QoSReliabilityPolicy.BEST_EFFORT

        self.create_subscription(
            TileCatalog, namespace + '/coverage_catalog',
            self._on_catalog, latched)
        self.create_subscription(
            SonarVisualizationTile, namespace + '/coverage_tiles',
            self._on_tile, best_effort)
        self._requests = self.create_publisher(
            TileRequest, namespace + '/coverage_requests', 10)

        self.create_timer(float(self._param('request_interval')),
                          self._send_requests)
        self.create_timer(float(self._param('render_interval')),
                          self._render_dirty)

        target = (self.local_dir if self.dry_run
                  else 's3://{}/{}'.format(self.bucket, self.prefix))
        self.get_logger().info(
            "coverage '{}' from {} -> {} at z{}".format(
                self.band_name, namespace, target, self.zoom))

    def _param(self, name):
        """Return a declared parameter's value."""
        return self.get_parameter(name).value

    # -- transport ---------------------------------------------------------

    def _on_catalog(self, msg):
        """Reconcile the cache against a complete catalog snapshot."""
        generation = tile_util.time_to_nanoseconds(msg.header.stamp)
        entries = [((e.index.level, e.index.row, e.index.col),
                    tile_util.time_to_nanoseconds(e.version))
                   for e in msg.entries]
        with self._lock:
            to_request, to_prune = self._reconciler.reconcile(
                entries, generation)
            for index in to_prune:
                self._reconciler.drop(index)
                removed = self._tiles.pop(index, None)
                self._applied.pop(index, None)
                if removed is not None:
                    self._mark_dirty(index)
            self._pending_request = to_request
        if to_request or to_prune:
            self.get_logger().info(
                'catalog: {} entries -> request {}, prune {}'.format(
                    len(entries), len(to_request), len(to_prune)))

    def _send_requests(self):
        """Ask the source for the tiles we are missing or stale on."""
        try:
            self._publish_requests()
        except Exception as error:
            # Same reasoning as the render timer: an escape here kills the
            # timer, and the node then never asks for another tile while
            # looking perfectly healthy.
            self._failures += 1
            self.get_logger().error(
                'request publication failed: {}'.format(error),
                throttle_duration_sec=10.0)

    def _publish_requests(self):
        """Publish one TileRequest for the pending set."""
        with self._lock:
            wanted = self._pending_request
            self._pending_request = []
        if not wanted:
            return
        msg = TileRequest()
        msg.header.stamp = self.get_clock().now().to_msg()
        for level, row, col in wanted:
            index = TileIndex()
            index.level = level
            index.row = row
            index.col = col
            msg.tiles.append(index)
        self._requests.publish(msg)

    def _tile_is_sane(self, index, msg):
        """Return True if a tile message can be trusted to allocate from."""
        if not is_valid_index(index):
            self.get_logger().warn(
                'ignoring tile with invalid grid index {}'.format(index),
                throttle_duration_sec=10.0)
            return False
        cells = int(msg.width) * int(msg.height)
        if (msg.width == 0 or msg.height == 0
                or msg.width > MAX_TILE_EDGE or msg.height > MAX_TILE_EDGE
                or cells * 4 > MAX_TILE_BYTES):
            self.get_logger().warn(
                'ignoring tile {} with implausible dimensions {}x{}'.format(
                    index, msg.width, msg.height),
                throttle_duration_sec=10.0)
            return False
        return True

    def _on_tile(self, msg):
        """Patch a dirty sub-window into its tile."""
        index = (msg.index.level, msg.index.row, msg.index.col)
        if not self._tile_is_sane(index, msg):
            return
        band = next((b for b in msg.bands if b.name == self.band_name), None)
        if band is None:
            return
        try:
            values = tile_util.decode_band(
                band.dtype, band.data, msg.window_width, msg.window_height,
                band.scale, band.offset, band.nodata)
        except ValueError as error:
            # A malformed patch is the source's problem, not ours: drop it and
            # let the next catalog round re-request the tile in full.
            self._failures += 1
            self.get_logger().warn('bad tile {}: {}'.format(index, error))
            return

        version = tile_util.time_to_nanoseconds(msg.header.stamp)
        # Possession is what the catalog reconciles against, and it means "I
        # hold this tile at this version" -- which a dirty sub-window does not
        # establish. Recording it for a partial patch is what silently defeats
        # the healing this protocol is built on: lose one best-effort window,
        # receive the next, and the cache now claims the catalog's newest
        # version while holding a hole that nothing will ever re-request. Only
        # a whole-tile message advances possession; a partial one updates the
        # pixels and leaves the catalog free to re-serve the tile in full.
        # (The producer serves requests as whole tiles -- quantize_tile.cpp:98
        # sets the window to the full grid -- so this costs nothing today and
        # keeps the guarantee if sub-window pushes ever arrive.)
        complete = (msg.window_row == 0 and msg.window_col == 0
                    and msg.window_width == msg.width
                    and msg.window_height == msg.height)
        with self._lock:
            # Newest-wins, BEFORE the patch lands. Checking only the
            # reconciler after the fact let a reordered stale window overwrite
            # fresh cells while the advertised version stayed newer, so the
            # damage was invisible to the catalog and never healed.
            applied = self._applied.get(index)
            if applied is not None and version < applied:
                self.get_logger().debug(
                    'dropping stale patch for {} ({} < {})'.format(
                        index, version, applied))
                return
            held = self._tiles.get(index)
            if held is None:
                held = tile_util.new_tile(msg.width, msg.height)
                self._tiles[index] = held
            try:
                tile_util.apply_window(
                    held, values, msg.window_row, msg.window_col)
            except ValueError as error:
                self._failures += 1
                self.get_logger().warn(
                    'window does not fit {}: {}'.format(index, error))
                # A first patch that did not fit leaves an all-NaN entry
                # behind, which renders as "no coverage here" and blocks the
                # re-request that would fix it. Drop it instead.
                if not numpy.isfinite(self._tiles[index]).any():
                    del self._tiles[index]
                return
            self._applied[index] = version
            if complete:
                self._reconciler.mark_have(index, version)
            self._mark_dirty(index)

    def _mark_dirty(self, index):
        """Mark every slippy tile that overlaps a GGGS tile for re-render.

        Bounded: a grid coarse enough to span a continent would otherwise
        enumerate millions of slippy tiles here, on the executor thread, from
        a single message.
        """
        level, row, col = index
        south, west, north, east = gggs.grid_bounds(level, row, col)
        marked = []
        for xy in gggs.tiles_covering(self.zoom, south, west, north, east):
            marked.append(xy)
            if len(marked) > MAX_DIRTY_TILES_PER_GRID:
                self.get_logger().warn(
                    'grid {} spans more than {} tiles at z{} -- ignoring it; '
                    'the zoom parameter and the source level disagree'.format(
                        index, MAX_DIRTY_TILES_PER_GRID, self.zoom),
                    throttle_duration_sec=30.0)
                return
        self._dirty.update(marked)

    # -- rendering ---------------------------------------------------------

    def _sample_tile(self, x, y):
        """Return a 256x256 float array of the band over one slippy tile.

        Nearest-neighbour from the GGGS cache. Cells with no coverage stay NaN
        and render transparent, which is what makes this a *coverage* layer --
        the shape of the surveyed area is the information.
        """
        south, west, north, east = gggs.tile_bounds(self.zoom, x, y)
        # Pixel centres, north-to-south to match image row order.
        lons = west + (numpy.arange(256) + 0.5) * (east - west) / 256.0
        lats = north - (numpy.arange(256) + 0.5) * (north - south) / 256.0
        out = numpy.full((256, 256), numpy.nan, dtype=numpy.float32)

        with self._lock:
            held = dict(self._tiles)
        if not held:
            return out

        for row_index, latitude in enumerate(lats):
            for index, array in held.items():
                level, grid_row, grid_col = index
                g_south, g_west, g_north, g_east = gggs.grid_bounds(
                    level, grid_row, grid_col)
                if not (g_south <= latitude < g_north):
                    continue
                inside = (lons >= g_west) & (lons < g_east)
                if not inside.any():
                    continue
                rows, cols = array.shape
                # GGGS cell rows are numbered FROM THE SOUTH
                # (gggs/cell_index.h:41,75,162 "positive integer row starting
                # the bottom of the grid"; confirmed by CAMP's explicit flip in
                # sonar_live_tile.cpp), while image rows run north to south.
                # Getting this backwards mirrors every tile vertically inside
                # its own ~870 m box at level 10 -- which does not look like a
                # flip, it looks like coverage drifting off the vessel's track.
                cell_row = int((latitude - g_south) / (g_north - g_south)
                               * rows)
                cell_row = max(0, min(rows - 1, cell_row))
                cell_cols = ((lons[inside] - g_west) / (g_east - g_west)
                             * cols).astype(int)
                cell_cols = numpy.clip(cell_cols, 0, cols - 1)
                out[row_index, inside] = array[cell_row, cell_cols]
        return out

    def _update_datum_offset(self):
        """Refresh the chart-datum offset from TF; invalidate on a real move.

        The `depth` band does NOT carry depth below chart datum. It carries z
        in the MAP frame, which is ellipsoidal: over the Piscataqua that reads
        -36 to -57 m, which saturates a 0-40 m ramp and paints essentially all
        coverage the deepest colour. Referenced to chart datum the same water
        is 8 to 29 m, on scale and agreeing with the basemap. The offset is the
        tide, so it MOVES -- it cannot be a constant, and it is read from
        `map_frame -> chart_datum_frame` exactly as `s57_layer.cpp` reads its
        own tide offset, with the same invalidate-past-a-threshold treatment so
        a moving tide re-renders and TF jitter does not.

        Returns True when a usable offset is held.
        """
        if self._tf_buffer is None:
            return True                      # correction disabled by config
        try:
            transform = self._tf_buffer.lookup_transform(
                self.map_frame, self.chart_datum_frame, Time())
        except tf2_ros.TransformException as error:
            self.get_logger().warn(
                'no chart-datum offset ({} in {}): {}'.format(
                    self.chart_datum_frame, self.map_frame, error),
                throttle_duration_sec=10.0)
            return self._datum_offset is not None
        # Height of chart datum expressed in the map frame, so a sounding's
        # depth below datum is (datum_z - z).
        offset = float(transform.transform.translation.z)
        if self._datum_offset is None:
            self._datum_offset = offset
            self.get_logger().info(
                'chart datum is {:.2f} m in {}; colouring depth below '
                'datum'.format(offset, self.map_frame))
            return True
        if abs(offset - self._datum_offset) > self.tide_invalidate_threshold:
            self.get_logger().info(
                'chart-datum offset moved {:.2f} -> {:.2f} m; re-rendering '
                'the mosaic'.format(self._datum_offset, offset))
            self._datum_offset = offset
            with self._lock:
                for index in list(self._tiles):
                    self._mark_dirty(index)
        return True

    def _colourise(self, values, datum_z):
        """Return RGBA bytes for a sampled tile; NaN becomes transparent.

        `datum_z` is the height of chart datum in the frame the values are
        expressed in, so depth below datum is `datum_z - value`.
        """
        rgba = numpy.zeros((256, 256, 4), dtype=numpy.uint8)
        present = numpy.isfinite(values)
        if not present.any():
            return rgba
        # Only cast the covered cells: casting NaN to uint8 is undefined and
        # numpy warns. The result happens to be hidden by alpha today, but it
        # would surface as garbage the moment the alpha rule changed.
        depth = numpy.clip(datum_z - values[present], 0.0, MAX_DEPTH)
        scaled = numpy.zeros(values.shape, dtype=numpy.uint8)
        scaled[present] = (depth / MAX_DEPTH * 255.0).astype(numpy.uint8)
        rgba[..., :3] = self._colours[scaled]
        rgba[..., 3] = numpy.where(present, 255, 0)
        return rgba

    def _render_one(self, x, y, datum_z):
        """Render and publish one slippy tile.

        Returns 'published', 'skipped' (nothing to do) or 'failed' (the tile
        stays dirty and is retried).
        """
        key = '{}/{}/{}/{}.png'.format(self.prefix, self.zoom, x, y)
        values = self._sample_tile(x, y)
        if not numpy.isfinite(values).any():
            # Coverage here was pruned or evicted. Skipping the upload leaves
            # the last PNG standing in the bucket, so the display keeps
            # showing coverage the source no longer holds -- and CloudFront
            # will keep serving it. Replace it with a transparent tile.
            if (x, y) not in self._published:
                return 'skipped'
            if not self._publish(empty_png(), key):
                return 'failed'
            self._published.discard((x, y))
            return 'published'

        image = Image.fromarray(
            self._colourise(values, datum_z), mode='RGBA')
        buffer = io.BytesIO()
        image.save(buffer, format='PNG', optimize=True)
        if not self._publish(buffer.getvalue(), key):
            return 'failed'
        self._published.add((x, y))
        return 'published'

    def _render_dirty(self):
        """Render and publish every slippy tile marked dirty."""
        if not self._update_datum_offset():
            # Without the offset every depth would be coloured from an
            # unreferenced ellipsoidal height -- wrong, and wrong in a way
            # that looks plausible. Leave the tiles dirty and try next pass.
            return
        datum_z = self._datum_offset or 0.0
        with self._lock:
            pending = sorted(self._dirty)
            self._dirty.clear()
        if not pending:
            return

        published = 0
        retry = set()
        for x, y in pending:
            try:
                outcome = self._render_one(x, y, datum_z)
            except Exception as error:
                # This runs on a timer. An exception here does not skip a
                # tile, it ends the timer and the node keeps spinning with
                # nothing rendering -- silently, because the subscriptions go
                # on working. Contain it per tile.
                self._failures += 1
                self.get_logger().error(
                    'rendering {},{} failed: {}'.format(x, y, error),
                    throttle_duration_sec=10.0)
                outcome = 'failed'
            if outcome == 'published':
                published += 1
            elif outcome == 'failed':
                retry.add((x, y))

        if retry:
            # The dirty set was cleared before rendering, so a failed upload
            # used to be a permanent hole in the mosaic: nothing would mark
            # that tile again until its GGGS grid changed. Put it back.
            with self._lock:
                self._dirty.update(retry)
            self.get_logger().warn(
                '{} tile(s) will be retried next pass'.format(len(retry)),
                throttle_duration_sec=30.0)

        self._rendered += published
        if published:
            self.get_logger().info(
                'rendered {} tile(s) at z{} ({} total, {} tiles cached)'.format(
                    published, self.zoom, self._rendered, len(self._tiles)))

    def _publish(self, payload, key):
        """Write one PNG locally or to S3; return True on success."""
        if self.dry_run:
            path = os.path.join(self.local_dir, key)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            # Atomic: a viewer polling the directory must never read a
            # half-written PNG.
            handle, temporary = tempfile.mkstemp(dir=os.path.dirname(path))
            with os.fdopen(handle, 'wb') as output:
                output.write(payload)
            os.replace(temporary, path)
            return True

        command = [
            'aws', 's3', 'cp', '-', 's3://{}/{}'.format(self.bucket, key),
            '--content-type', 'image/png',
            '--cache-control', 'max-age={}'.format(self.cache_control),
            '--profile', self.profile,
        ]
        try:
            result = subprocess.run(command, input=payload,
                                    capture_output=True, timeout=30)
        except subprocess.TimeoutExpired:
            self._failures += 1
            self.get_logger().error('upload of {} timed out'.format(key))
            return False
        if result.returncode != 0:
            self._failures += 1
            self.get_logger().error('upload of {} failed: {}'.format(
                key, result.stderr.decode().strip()[:200]))
            return False
        return True


def main(args=None):
    """Spin the coverage renderer until interrupted."""
    rclpy.init(args=args)
    node = CoverageRenderer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.get_logger().info(
            'stopping: {} tiles rendered, {} cached, {} failures'.format(
                node._rendered, len(node._tiles), node._failures))
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
