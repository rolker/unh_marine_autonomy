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
import json
import os
import tempfile
import threading
import time

from marine_interfaces.msg import SonarVisualizationTile
from marine_interfaces.msg import TileCatalog
from marine_interfaces.msg import TileIndex
from marine_interfaces.msg import TileRequest

from marine_web_view import gggs
from marine_web_view import tiles as tile_util
from marine_web_view.reconciler import is_valid_index
from marine_web_view.reconciler import TileCatalogReconciler
from marine_web_view.s3_upload import describe_error
from marine_web_view.s3_upload import S3Uploader

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

# Slippy zoom is a shift width, and the parameter carrying it is external
# input. A negative zoom raises "negative shift count" deep inside _mark_dirty
# -- uncontained in the subscription callback, so the node dies on the first
# tile it receives -- and an absurdly large one silently renders nothing,
# because every grid then trips MAX_DIRTY_TILES_PER_GRID. 22 is the deepest
# level the page's layers offer (maxZoom: 22 in web/index.html).
DEFAULT_ZOOM = 15
DEFAULT_PREFIX = 'live/coverage'
MAX_SLIPPY_ZOOM = 22

# How long the shutdown flush may run. The 45 s join above it exists so that a
# stop cannot hang on an in-flight upload; following it with an unbounded pass
# gives exactly that back -- a pass is one 30 s-capped upload per dirty tile,
# and a large mosaic with a failing S3 endpoint would sit there for hours with
# the operator's Ctrl-C already spent. Whatever does not fit inside the
# deadline is lost, which is precisely what would have been lost with no flush
# at all.
SHUTDOWN_FLUSH_SECONDS = 30.0

# How long the render thread waits for a response that has stopped arriving.
# Not a per-PUT ceiling (see `_boto3_client`); its job is to stop one dead
# connection holding the render thread forever.
UPLOAD_READ_TIMEOUT = 25

# How long `stop()` waits for the render thread to notice the stop event. The
# thread checks it between tiles, so this only has to cover the request it is
# already inside -- and it is a daemon thread, so losing the race costs a
# warning and a skipped flush, never a hang.
WORKER_JOIN_SECONDS = 10.0

# How old the transform the chart-datum offset came from may get before the
# manifest says so. Age OF THE DATA -- see `_transform_seconds` and
# `CoverageRenderer._datum_age`: the lookup itself keeps succeeding after the
# publisher dies, so the time of the last lookup measures nothing.
#
# The offset is the tide: it moves, and a stale one colours live coverage
# against an old water level -- wrong, and wrong in a way that looks entirely
# plausible on the page. A total TF outage after the first successful lookup
# is the one degradation the manifest used to hide behind `status: 'ok'`,
# because the node keeps rendering happily from the last value it saw. Three
# render intervals at the shipped 20 s cadence.
DATUM_STALE_SECONDS = 60.0


# Timer periods come from parameters too, and rclpy's create_timer rejects a
# non-positive period with a ValueError. That is the right answer -- but it
# used to be raised AFTER the render worker had been started, from a
# constructor whose caller then held no node to stop it, leaving a live thread
# behind a failed start. Validate first, and stand the worker up last.
DEFAULT_RENDER_INTERVAL = 20.0
DEFAULT_REQUEST_INTERVAL = 5.0
MAX_INTERVAL_SECONDS = 86400.0

# The manifest gets a SHORTER max-age than the tiles it advertises. It is the
# liveness signal, not payload: a few hundred bytes read once per poll, and
# every second a cache holds it is a second in which the page under-reports
# the renderer's age and does not learn that new tiles exist. Matching it to
# `cache_control` (i.e. to the render interval, which is also the page's poll
# period) means two viewers can be shown a manifest from the previous pass.
# It is not set to zero: a hard ceiling on origin requests is worth keeping on
# a public page with unbounded viewers, and it is what makes the request cost
# independent of viewer count. At 5 s that ceiling is 12 origin GETs a minute
# no matter how many people are watching -- ~13k more S3 GETs a day than a
# 20 s max-age, on the order of $0.15 a month, for a fourfold cut in the
# worst-case staleness of the one object the display's liveness depends on.
# Never longer than the tiles' own max-age: an operator who shortens
# `cache_control` is asking for a fresher display, not a fresher manifest only.
META_MAX_AGE_SECONDS = 5


def meta_max_age(cache_control):
    """Return the manifest's `max-age`, given the tiles' own.

    Never longer than the tiles' (an operator who shortens `cache_control`
    wants a fresher display, not a fresher manifest only) and never below 1,
    which is not a valid `max-age`.
    """
    return max(1, min(META_MAX_AGE_SECONDS, int(cache_control)))


def _transform_seconds(transform):
    """Return a transform's own stamp in seconds, or None if it has none.

    This is the age of the DATA, which is what staleness has to be measured
    from; see `CoverageRenderer._datum_age`.

    A zero stamp is returned as None rather than as 1970. tf2 answers a
    `Time()` query against a STATIC transform by handing back the requested
    time -- zero -- so a static chart-datum publisher (an opt-out for water
    with no tide correction, and a legitimate configuration) would otherwise
    read as decades stale and dim the layer forever. A static datum cannot go
    stale, so it has no age.
    """
    stamp = getattr(getattr(transform, 'header', None), 'stamp', None)
    if stamp is None:
        return None
    seconds = float(getattr(stamp, 'sec', 0)) + \
        float(getattr(stamp, 'nanosec', 0)) / 1e9
    return seconds if seconds > 0.0 else None


def sane_interval(value, default):
    """Return `(seconds, True)`, or `(default, False)` if unusable."""
    try:
        seconds = float(value)
    except (TypeError, ValueError):
        return default, False
    if seconds > 0.0 and seconds <= MAX_INTERVAL_SECONDS:
        return seconds, True
    return default, False


def sane_zoom(zoom):
    """Return `(zoom, True)`, or `(DEFAULT_ZOOM, False)` if it is unusable."""
    try:
        zoom = int(zoom)
    except (TypeError, ValueError):
        return DEFAULT_ZOOM, False
    if 0 <= zoom <= MAX_SLIPPY_ZOOM:
        return zoom, True
    return DEFAULT_ZOOM, False


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


def _is_usable_bucket(bucket):
    """Return True if a string could be an S3 bucket name.

    Not a full validation of AWS's naming rules -- just enough to catch the
    parameter that makes every upload fail forever: empty, or carrying the
    separators that would make `s3://<bucket>/<key>` mean something else.
    """
    if not bucket or len(bucket) > 63:
        return False
    return not any(character in bucket for character in ' \t/:')


def _safe_prefix(prefix):
    """Return a bucket/directory prefix with no traversal in it.

    In dry-run the prefix is joined onto a local directory that is then
    served, so `../..` in a parameter escapes it. Empty segments and `.` are
    dropped too, so the key stays the same shape in the bucket and on disk.
    """
    parts = [part for part in str(prefix).split('/')
             if part not in ('', '.', '..')]
    return '/'.join(parts)


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
        self.declare_parameter('zoom', DEFAULT_ZOOM)
        self.declare_parameter('bucket', 'unh-ccom-p11-live')
        self.declare_parameter('prefix', DEFAULT_PREFIX)
        self.declare_parameter('profile', 'p11-renderer')
        self.declare_parameter('render_interval', DEFAULT_RENDER_INTERVAL)
        self.declare_parameter('request_interval', DEFAULT_REQUEST_INTERVAL)
        self.declare_parameter('dry_run', False)
        self.declare_parameter('local_dir', '/tmp/coverage')
        # Matched to render_interval by default: a tile held longer than
        # the interval that replaces it is stale for no reason.
        self.declare_parameter('cache_control', 20)
        self.declare_parameter('cache_budget_bytes', 512 * 1024 * 1024)
        self.declare_parameter('max_requests_per_message', 256)
        self.declare_parameter('map_frame', DEFAULT_MAP_FRAME)
        self.declare_parameter('chart_datum_frame', DEFAULT_CHART_DATUM_FRAME)
        self.declare_parameter('tide_invalidate_threshold',
                               DEFAULT_TIDE_INVALIDATE_THRESHOLD)

        self.band_name = self._param('band')
        self.zoom, zoom_ok = sane_zoom(self._param('zoom'))
        if not zoom_ok:
            self.get_logger().warn(
                'zoom {} is not a usable slippy level (0-{}); rendering at '
                '{} instead'.format(self._param('zoom'), MAX_SLIPPY_ZOOM,
                                    self.zoom))
        self.bucket = str(self._param('bucket')).strip()
        self.prefix = _safe_prefix(str(self._param('prefix')))
        if not self.prefix:
            # An empty prefix (or one made empty by the traversal scrub) puts
            # a leading slash on every key: os.path.join(local_dir, '/15/..')
            # discards local_dir entirely, the realpath guard then refuses the
            # write, and every object fails and retries for the life of the
            # node. Refuse at startup instead of failing per tile forever.
            self.get_logger().warn(
                "prefix {!r} is empty after scrubbing; using '{}'".format(
                    self._param('prefix'), DEFAULT_PREFIX))
            self.prefix = DEFAULT_PREFIX
        # An empty profile is not an error: it means "use the default
        # credential chain", which is what a machine with an instance role or
        # a plain ~/.aws/credentials wants. Handing boto3 an empty profile
        # name instead fails every upload, so it is coalesced to None where
        # the uploader is built (below).
        self.profile = str(self._param('profile')).strip()
        self.dry_run = bool(self._param('dry_run'))
        if not self.dry_run and not _is_usable_bucket(self.bucket):
            # `prefix` is carefully normalised while the bucket -- the other
            # half of every key -- went unchecked. An empty or malformed one
            # is not recoverable by falling back to a default: that would
            # quietly publish a survey's coverage somewhere nobody asked for.
            # It is also not survivable, because every upload becomes a
            # doomed S3 PUT inside a retry loop that never drains.
            # Refuse to start, loudly, while an operator is still watching.
            raise ValueError(
                "bucket {!r} is not a usable S3 bucket name; set 'bucket', "
                "or set 'dry_run' to write to local_dir instead".format(
                    self._param('bucket')))
        if not self.dry_run and not self.profile:
            self.get_logger().info(
                'no AWS profile configured; using the default credential '
                'chain')
        self.local_dir = self._param('local_dir')
        self.cache_control = int(self._param('cache_control'))
        # max-age is what makes a viewer come back for a new tile, so it has
        # to be at most the interval at which new tiles appear: at the shipped
        # 60 s against a 20 s render the display was up to a minute stale for
        # no reason. Zero or negative is not a valid max-age at all.
        self.render_interval, render_ok = sane_interval(
            self._param('render_interval'), DEFAULT_RENDER_INTERVAL)
        if not render_ok:
            self.get_logger().warn(
                'render_interval {} is not a usable period; using {:g} s'
                .format(self._param('render_interval'), self.render_interval))
        self.request_interval, request_ok = sane_interval(
            self._param('request_interval'), DEFAULT_REQUEST_INTERVAL)
        if not request_ok:
            self.get_logger().warn(
                'request_interval {} is not a usable period; using {:g} s'
                .format(self._param('request_interval'),
                        self.request_interval))
        render_interval = self.render_interval
        if self.cache_control < 1:
            self.get_logger().warn(
                'cache_control {} is not a valid max-age; using {}'.format(
                    self.cache_control, int(max(1.0, render_interval))))
            self.cache_control = int(max(1.0, render_interval))
        elif self.cache_control > render_interval:
            self.get_logger().warn(
                'cache_control {} s exceeds render_interval {:g} s: '
                'viewers will hold a tile past its replacement'.format(
                    self.cache_control, render_interval))
        # Derived, not a parameter: there is one right answer for a liveness
        # manifest (see META_MAX_AGE_SECONDS) and a knob here would only let
        # an operator turn the freshness of the heartbeat back off.
        self.meta_cache_control = meta_max_age(self.cache_control)
        self.cache_budget_bytes = int(self._param('cache_budget_bytes'))
        self.max_requests_per_message = max(
            1, int(self._param('max_requests_per_message')))
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

        # Not on a dry run: writing under local_dir needs no AWS access at
        # all, and constructing a client would go looking for credentials a
        # simulator host has no reason to carry. `or None` is deliberate and
        # preserves this node's long-standing behaviour: an empty profile
        # means "use the default credential chain" -- an EC2 instance role or
        # a plain ~/.aws/credentials -- which is what the 'no AWS profile
        # configured' info line above announces. Passing '' instead would
        # fail every upload: boto3.Session would look for a profile
        # literally named ''.
        # state_renderer deliberately does NOT coalesce; see its own comment.
        # read_timeout caps the render thread's wait on a response that has
        # stopped arriving. It is NOT a per-PUT ceiling -- no exact one
        # exists to quote, see _boto3_client -- and nothing in this node's
        # shutdown is sized against it: the render pass checks an abort
        # predicate BETWEEN tiles, so a stop takes effect without waiting on
        # an upload already in flight.
        self._uploader = (None if self.dry_run else
                          S3Uploader(self.bucket,
                                     profile=self.profile or None,
                                     read_timeout=UPLOAD_READ_TIMEOUT))

        self._reconciler = TileCatalogReconciler()
        self._tiles = {}          # index -> full-tile float array
        self._applied = {}        # index -> newest patch version applied
        self._touch = {}          # index -> LRU sequence number
        self._touch_seq = 0
        self._cache_bytes = 0
        self._dirty = set()       # slippy tiles needing a re-render
        self._lock = threading.Lock()
        self._colours = colour_table()
        self._pending_request = []
        self._published = set()   # slippy tiles currently holding coverage
        self._rendered = 0
        self._failures = 0
        # A lock of its own, deliberately: _on_tile counts a bad tile while
        # holding self._lock, and self._lock is not reentrant.
        self._failure_lock = threading.Lock()
        self._datum_offset = None
        # ROS time, in seconds, OF THE TRANSFORM DATA -- not of the lookup.
        # See `_datum_age`: a lookup at Time() keeps succeeding forever
        # after the publisher dies, so the wall clock of the last lookup
        # measures nothing.
        self._datum_stamp = None

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
        # Depth 10, matching the producer (cube_bathymetry_node.cpp:513) and
        # CAMP's consumer. A deeper queue on a BEST_EFFORT topic does not make
        # the transport less lossy, it just buys a longer backlog of tiles
        # that are already superseded -- ~92 MB of them at depth 50 -- to work
        # through before the render sees current data.
        best_effort = QoSProfile(depth=10)
        best_effort.reliability = QoSReliabilityPolicy.BEST_EFFORT

        self.create_subscription(
            TileCatalog, namespace + '/coverage_catalog',
            self._on_catalog, latched)
        self.create_subscription(
            SonarVisualizationTile, namespace + '/coverage_tiles',
            self._on_tile, best_effort)
        self._requests = self.create_publisher(
            TileRequest, namespace + '/coverage_requests', 10)

        # Rendering runs on its own thread, not on the executor. A pass
        # samples, encodes and uploads -- with a 30 s timeout per object --
        # and doing that in a timer callback blocks the single-threaded
        # executor for the whole pass, during which every BEST_EFFORT tile
        # pushed by the source is dropped on the floor. The timer only rings
        # the bell.
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._worker = threading.Thread(
            target=self._render_loop, name='coverage_render', daemon=True)

        self.create_timer(self.request_interval, self._send_requests)
        self.create_timer(self.render_interval, self._wake_renderer)

        # Started LAST, once nothing left in the constructor can raise. A
        # thread started earlier outlives a constructor that then fails:
        # `main()` is left with `node is None`, never calls `stop()`, and the
        # worker goes on rendering and uploading against a half-built node.
        self._worker.start()

        target = (self.local_dir if self.dry_run
                  else 's3://{}/{}'.format(self.bucket, self.prefix))
        self.get_logger().info(
            "coverage '{}' from {} -> {} at z{}".format(
                self.band_name, namespace, target, self.zoom))

    def _param(self, name):
        """Return a declared parameter's value."""
        return self.get_parameter(name).value

    def _note_failure(self):
        """Count one failure, from whichever thread hit it.

        `+=` on an int is a read-modify-write, and this counter is the one
        piece of shared mutable state the copy-on-write discipline does not
        cover: the executor thread counts malformed tiles and failed request
        publications while the render thread counts failed uploads. Concurrent
        increments lose counts, and this number is the whole of what the node
        reports about what went wrong.
        """
        with self._failure_lock:
            self._failures += 1

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
                self._touch.pop(index, None)
                if removed is not None:
                    self._cache_bytes -= removed.nbytes
                    self._mark_dirty(index)
            # A fresh catalog supersedes the queue outright: anything still
            # wanted reappears in to_request, and anything served since is
            # correctly gone. The batching in _publish_requests works through
            # this list in order, and each round's reconcile drops what has
            # arrived, so the tail is reached rather than starved.
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
            self._note_failure()
            self.get_logger().error(
                'request publication failed: {}'.format(error),
                throttle_duration_sec=10.0)

    def _publish_requests(self):
        """Publish one TileRequest for the pending set.

        Batched. A cold start against a large store asks for the whole
        catalog at once, and `coverage_requests` is a shared fanout: the
        source serves every resident index in the message in one callback,
        so an unbounded request is an unbounded burst on the producer and on
        every other consumer's tile topic. The remainder is carried to the
        next interval, which is the throttle.
        """
        with self._lock:
            wanted = self._pending_request[:self.max_requests_per_message]
            self._pending_request = (
                self._pending_request[self.max_requests_per_message:])
            remaining = len(self._pending_request)
        if not wanted:
            return
        msg = TileRequest()
        msg.header.stamp = self.get_clock().now().to_msg()
        # The producer's own consumer stamps this; matching it keeps the
        # index frame explicit rather than empty (CAMP: sonar_live_cache_layer
        # .cpp:274).
        msg.header.frame_id = 'gggs'
        for level, row, col in wanted:
            index = TileIndex()
            index.level = level
            index.row = row
            index.col = col
            msg.tiles.append(index)
        self._requests.publish(msg)
        if remaining:
            self.get_logger().info(
                'requested {} tile(s), {} still queued'.format(
                    len(wanted), remaining),
                throttle_duration_sec=10.0)

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
            self._note_failure()
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
            if held is not None and held.shape != (msg.height, msg.width):
                # The grid was re-cut: a level's tile geometry changed under
                # us. Patching the new window into the old array is wrong in
                # both directions -- a larger window makes apply_window raise
                # on every message forever (the index wedges: possession and
                # _applied never advance, so it is re-requested and re-fails),
                # and a smaller one lands the cells at the wrong offsets and
                # mis-georeferences silently. Forget the tile and rebuild.
                self.get_logger().warn(
                    'tile {} changed geometry {} -> {}; rebuilding'.format(
                        index, held.shape, (msg.height, msg.width)))
                self._forget(index)
                held = None
            if held is None:
                held = tile_util.new_tile(msg.width, msg.height)
                self._cache_bytes += held.nbytes
            else:
                # Copy-on-write. The renderer samples these arrays outside the
                # lock, so patching in place would let it read a half-written
                # tile -- and the lock, which only ever guarded the dict, was
                # decoration against that. Replacing the array wholesale makes
                # every array the renderer holds immutable.
                held = held.copy()
            self._tiles[index] = held
            try:
                tile_util.apply_window(
                    held, values, msg.window_row, msg.window_col)
            except ValueError as error:
                self._note_failure()
                self.get_logger().warn(
                    'window does not fit {}: {}'.format(index, error))
                # A first patch that did not fit leaves an all-NaN entry
                # behind, which renders as "no coverage here" and blocks the
                # re-request that would fix it. Drop it instead -- all of it:
                # popping only _tiles left _applied, _touch and reconciler
                # possession claiming a tile the node no longer holds, so the
                # catalog never re-requested it and the hole was permanent.
                if not numpy.isfinite(self._tiles[index]).any():
                    self._forget(index)
                return
            self._applied[index] = version
            self._touch_seq += 1
            self._touch[index] = self._touch_seq
            if complete:
                self._reconciler.mark_have(index, version)
            self._mark_dirty(index)
            self._evict_if_over_budget()

    def _forget(self, index):
        """Drop every trace of a cached tile. Call with the lock held.

        The cache is four pieces of state that have to move together: the
        array, the applied version, the LRU stamp and the reconciler's
        possession. Dropping the array alone leaves the catalog believing we
        hold a tile we do not, so it is never re-served and the gap never
        heals -- which is the failure the possession bookkeeping exists to
        prevent in the first place.
        """
        array = self._tiles.pop(index, None)
        if array is not None:
            self._cache_bytes -= array.nbytes
        self._applied.pop(index, None)
        self._touch.pop(index, None)
        self._reconciler.drop(index)

    def _evict_if_over_budget(self):
        """Drop least-recently-updated tiles until the cache fits its budget.

        Call with the lock held. A GGGS grid is 960 x 960 float32 -- 3.69 MB,
        about 19.6 MB per square kilometre surveyed -- so an unbounded cache
        is a slow memory leak with a survey-shaped growth curve. CAMP shipped
        a 512 MiB budget for exactly this cache.

        Possession is dropped along with the tile, so the next catalog round
        re-requests it: this consumer, unlike CAMP's, has no disk to fall back
        on, and keeping possession of a tile we no longer hold would let a
        later sub-window patch rebuild it from NaN and blank the coverage
        already published for it. The already-published PNG stands until then,
        so the display does not flicker -- an evicted tile is not marked
        dirty. Being over budget at all is abnormal, hence the warning.

        Not flickering holds for a slippy tile fed by one grid. One that
        straddles two is re-rendered when either changes, from whatever the
        cache still holds, so an evicted neighbour's half re-publishes
        transparent until the catalog re-serves it -- bounded to the seam and
        self-healing, but another reason eviction is a warning and not a
        routine operating point. Noted in the README.
        """
        if self.cache_budget_bytes <= 0 or not self._tiles:
            return
        if self._cache_bytes <= self.cache_budget_bytes:
            return
        order = sorted(self._touch.items(), key=lambda item: item[1])
        evicted = 0
        for index, _ in order:
            if self._cache_bytes <= self.cache_budget_bytes:
                break
            self._forget(index)
            evicted += 1
        if evicted:
            self.get_logger().warn(
                'cache over its {} MiB budget: evicted {} tile(s), {} left. '
                'They will be re-requested; raise cache_budget_bytes or '
                'render at a coarser level if this repeats.'.format(
                    self.cache_budget_bytes // (1024 * 1024), evicted,
                    len(self._tiles)),
                throttle_duration_sec=30.0)

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

    def _candidates(self, south, west, north, east):
        """Return the cached tiles overlapping a box, coarsest level first.

        Snapshotting under the lock is not enough on its own: the arrays go on
        being written by the transport thread. They are treated as immutable
        here and replaced wholesale on patch (see _on_tile), so what this
        returns is a stable view rather than a live one.

        Coarsest first so that where two levels cover the same ground the
        finer one lands last and wins. Resolving that by dict iteration order
        -- which is what happened before -- makes the display depend on
        arrival order.
        """
        with self._lock:
            held = list(self._tiles.items())
        candidates = []
        for index, array in held:
            bounds = gggs.grid_bounds(*index)
            g_south, g_west, g_north, g_east = bounds
            if g_north <= south or g_south >= north:
                continue
            if g_east <= west or g_west >= east:
                continue
            candidates.append((index[0], index, array, bounds))
        candidates.sort(key=lambda item: item[0])
        return candidates

    def _sample_tile(self, x, y):
        """Return a 256x256 float array of the band over one slippy tile.

        Nearest-neighbour from the GGGS cache. Cells with no coverage stay NaN
        and render transparent, which is what makes this a *coverage* layer --
        the shape of the surveyed area is the information.

        Vectorized over the whole 256x256 grid, and only over the tiles that
        actually overlap it. The row-by-row form this replaces was
        O(dirty x 256 x |cache|) in interpreted Python -- a whole-cache scan
        per image row -- which is what made a render pass long enough to
        matter.
        """
        south, west, north, east = gggs.tile_bounds(self.zoom, x, y)
        # Pixel centres, north-to-south to match image row order. Longitude is
        # linear across a slippy tile; latitude is NOT -- the tile is linear in
        # Mercator y -- so the rows are placed by inverting the projection
        # rather than by dividing the latitude span evenly. Spacing them in
        # latitude puts every row at a latitude it does not cover: sub-pixel at
        # zoom 15, a visible vertical stretch at the coarse zooms the parameter
        # admits.
        lons = west + (numpy.arange(256) + 0.5) * (east - west) / 256.0
        lats = numpy.array(gggs.tile_pixel_latitudes(self.zoom, y, 256))
        out = numpy.full((256, 256), numpy.nan, dtype=numpy.float32)

        for _, _, array, bounds in self._candidates(south, west, north, east):
            g_south, g_west, g_north, g_east = bounds
            rows_in = (lats >= g_south) & (lats < g_north)
            cols_in = (lons >= g_west) & (lons < g_east)
            if not rows_in.any() or not cols_in.any():
                continue
            rows, cols = array.shape
            # GGGS cell rows are numbered FROM THE SOUTH
            # (gggs/cell_index.h:41,75,162 "positive integer row starting
            # the bottom of the grid"; confirmed by CAMP's explicit flip in
            # sonar_live_tile.cpp), while image rows run north to south.
            # Getting this backwards mirrors every tile vertically inside
            # its own ~870 m box at level 10 -- which does not look like a
            # flip, it looks like coverage drifting off the vessel's track.
            cell_rows = ((lats[rows_in] - g_south) / (g_north - g_south)
                         * rows).astype(int)
            cell_rows = numpy.clip(cell_rows, 0, rows - 1)
            cell_cols = ((lons[cols_in] - g_west) / (g_east - g_west)
                         * cols).astype(int)
            cell_cols = numpy.clip(cell_cols, 0, cols - 1)
            patch = array[numpy.ix_(cell_rows, cell_cols)]
            # Write only the covered cells: a finer tile's NaN must not erase
            # a coarser tile's data underneath it.
            target = out[numpy.ix_(rows_in, cols_in)]
            covered = numpy.isfinite(patch)
            target[covered] = patch[covered]
            out[numpy.ix_(rows_in, cols_in)] = target
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
        self._datum_stamp = _transform_seconds(transform)
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

    def _datum_age(self):
        """Return the age of the chart-datum DATA, in seconds.

        Measured from the stamp on the transform, against the ROS clock -- not
        from when the lookup ran. This is the whole point: `lookup_transform`
        with `Time()` asks for the LATEST AVAILABLE transform, and tf2 prunes
        its buffer only when something is inserted, so a tide publisher that
        dies keeps resolving the same transform forever. Timing the lookup
        therefore reports a permanently fresh datum for a permanently frozen
        water level -- exactly the degradation `DATUM_STALE_SECONDS` exists to
        surface, reported as `status: 'ok'`.

        `None` when the correction is disabled by configuration, when no
        offset has ever been read, or when the transform carries no usable
        stamp (a static publisher -- see `_transform_seconds`). None of those
        is staleness.
        """
        if self._tf_buffer is None or self._datum_stamp is None:
            return None
        now = self.get_clock().now().nanoseconds / 1e9
        # Clamp: a transform stamped slightly ahead of this node's clock is
        # not negatively old, and a negative age would read as fresh anyway.
        return max(0.0, now - self._datum_stamp)

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

    def _wake_renderer(self):
        """Ask the render thread for a pass (timer callback)."""
        self._wake.set()

    def _render_loop(self):
        """Run render passes on demand until stopped."""
        while not self._stop.is_set():
            self._wake.wait()
            self._wake.clear()
            if self._stop.is_set():
                return
            try:
                # A scheduled pass aborts on the stop event. That is what
                # makes `stop()`'s join winnable by construction rather than
                # by hoping the upload of the tile in flight returns.
                self._render_dirty(self._stop.is_set)
            except Exception as error:
                # The per-tile containment is inside _render_dirty; this is
                # the backstop that keeps the thread itself alive.
                self._note_failure()
                self.get_logger().error(
                    'render pass failed: {}'.format(error),
                    throttle_duration_sec=10.0)

    def stop(self):
        """Stop the render thread, flushing whatever is still dirty.

        Without a final pass the tiles rendered since the last one are simply
        lost, which at the default cadence is up to 20 s of the end of a
        survey line -- the part an operator is most likely to be looking for.
        Unlike the position artifacts state_renderer publishes, a coverage
        tile is not a snapshot that the next one supersedes: drop it and that
        patch of seabed is simply missing until its grid changes again.

        Bounded BY CONSTRUCTION, without appealing to how long one PUT takes
        (there is no exact answer -- see `_boto3_client`). The worker checks
        the stop event between tiles, so the join wins unless it is inside a
        request; the flush stops at a wall-clock deadline it checks between
        tiles too; and neither waits on the other. Worst case is
        WORKER_JOIN_SECONDS + SHUTDOWN_FLUSH_SECONDS, plus the manifest PUT
        every pass ends with (truncated or not -- see `_render_dirty`), plus
        whatever single request was already in flight when the stop arrived.
        Neither of those last two is budgeted; both are single PUTs.
        """
        if self._stop.is_set():
            return
        self._stop.set()
        self._wake.set()
        try:
            self._worker.join(timeout=WORKER_JOIN_SECONDS)
            if self._worker.is_alive():
                # It is inside a request. The flush cannot run concurrently
                # with a live pass -- `_published` and `_rendered` are the
                # render thread's -- so it is skipped, and the tiles stay
                # unpublished. That is the honest outcome of a wedged
                # endpoint, and it costs a warning, not a hang: the worker
                # is a daemon thread.
                self.get_logger().warn(
                    'render thread still inside a request after {:g} s; '
                    'skipping the final flush'.format(WORKER_JOIN_SECONDS))
                return
            if self._dirty:
                self.get_logger().info(
                    'flushing {} dirty tile(s) before exit '
                    '(up to {:g} s)'.format(
                        len(self._dirty), SHUTDOWN_FLUSH_SECONDS))
                deadline = time.monotonic() + SHUTDOWN_FLUSH_SECONDS
                # The flush deliberately does NOT abort on `self._stop` --
                # it is set, that is why we are here. Its bound is the
                # deadline, checked on exactly the same path.
                self._render_dirty(lambda: time.monotonic() >= deadline)
        except KeyboardInterrupt:
            # An impatient second Ctrl-C lands in the join or in the flush.
            # This is called from `main()`'s `finally`, so letting it out
            # skips `destroy_node()` and `rclpy.shutdown()` entirely -- the
            # cleanup this method exists to make orderly. The worker is a
            # daemon thread; the process is going away regardless.
            self.get_logger().warn(
                'interrupted while stopping; the final flush was skipped')
        except Exception as error:
            self.get_logger().error(
                'final flush failed: {}'.format(error))

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

    def _publish_meta(self, status):
        """Publish the manifest the page configures its layer from.

        The page cannot be allowed to hardcode the zoom this node renders at:
        the two drifted apart silently, and a layer pinned to the wrong native
        zoom requests tiles that were never written. It also cannot tell "no
        coverage yet" from "this node died" -- a missing tile is the normal
        case, so the transparent errorTileUrl hides total failure as an empty
        map. This object is both the configuration and the heartbeat.
        """
        age = self._datum_age()
        # This runs on the render thread. `_tiles` is mutated by the executor
        # thread, so its size is read under the lock like every other access
        # to it -- and `render_interval` is read from the attribute cached at
        # construction rather than through `get_parameter`, which is not the
        # render thread's to call.
        with self._lock:
            cached_tiles = len(self._tiles)
            published_tiles = len(self._published)
        payload = json.dumps({
            'band': self.band_name,
            'cache_control': self.cache_control,
            'cached_tiles': cached_tiles,
            'chart_datum_offset': self._datum_offset,
            'max_depth': MAX_DEPTH,
            'prefix': self.prefix,
            'chart_datum_age': age,
            'published_tiles': published_tiles,
            'render_interval': self.render_interval,
            # The CHANGE SIGNAL, and the only field that says whether this
            # pass did anything. `stamp` is rewritten every pass, idle or
            # not, so a page that refreshes on a new stamp tears down and
            # re-requests every visible tile every `render_interval` for as
            # long as anyone has the page open -- with the sonar off and the
            # boat docked, per viewer, billed to us. This is the running
            # total of tiles this process has actually published; it does not
            # move on an idle pass. `published_tiles` cannot substitute: it
            # is the SIZE of the published set, so it does not move when an
            # already-published tile is re-rendered -- the common case as a
            # survey line grows inside a tile -- and it goes DOWN when
            # coverage is pruned. Read off the render thread, which is the
            # only thread that writes it (see `_render_pending`), so it needs
            # no lock. A restart resets it, so consumers must compare for
            # CHANGE, not for growth.
            'rendered_tiles': self._rendered,
            # WALL CLOCK, deliberately, not the ROS clock. The page computes
            # the manifest's age as Date.now()/1000 - stamp, and under
            # use_sim_time -- the documented simulator workflow -- the ROS
            # clock starts near zero, so every live pass would read as
            # decades stale and the heartbeat this object exists to be would
            # report a healthy renderer as dead. The ROS stamp is carried
            # alongside for anyone correlating against a bag.
            'ros_stamp': self.get_clock().now().nanoseconds / 1e9,
            'stamp': time.time(),
            'status': status,
            'zoom': self.zoom,
        }, sort_keys=True).encode()
        self._publish(payload, '{}/meta.json'.format(self.prefix),
                      content_type='application/json',
                      max_age=self.meta_cache_control)

    def _render_dirty(self, abort=None):
        """Run one render pass and publish the manifest for it.

        `abort` is a predicate the pass consults before every upload it
        issues; when it returns True the pass stops and hands whatever it has
        not done back to the dirty set. There is ALWAYS one -- a scheduled
        pass defaults to the stop event, the shutdown flush passes a
        wall-clock deadline -- because a pass with no way to stop is a pass
        whose length is set by the S3 endpoint. An earlier round made the
        check conditional on a `deadline` argument that only the flush
        passed, which left every scheduled pass unbounded and unstoppable.
        """
        if abort is None:
            abort = self._stop.is_set
        if abort():
            return
        if not self._update_datum_offset():
            # Without the offset every depth would be coloured from an
            # unreferenced ellipsoidal height -- wrong, and wrong in a way
            # that looks plausible. Leave the tiles dirty and try next pass.
            self._publish_meta('waiting_for_chart_datum')
            return
        self._render_pending(abort)
        if abort():
            # Out of budget, with tiles already PUT. The manifest is what
            # ANNOUNCES them: the page refreshes its layer only when
            # `rendered_tiles` moves (see index.html's pollCoverage), so
            # skipping this PUT means the tiles this pass did publish are
            # invisible until some later pass moves the counter -- and on the
            # shutdown flush, where truncation is likeliest, there is no later
            # pass. That is the exact loss the flush exists to prevent.
            # The manifest can say what happened rather than overclaim: the
            # `status` field is carried for precisely this, and the page
            # renders it verbatim.
            self._publish_meta('truncated_render')
            return
        # After the pass, so the counts the page shows are this pass's.
        age = self._datum_age()
        if age is not None and age > DATUM_STALE_SECONDS:
            # TF went away after we had an offset. Rendering continues from
            # the last value -- there is nothing better to do with live
            # coverage -- but the tide has moved on and the page has to be
            # able to say so, or a frozen water level reads as ordinary
            # bathymetry.
            self.get_logger().warn(
                'chart-datum offset is {:.0f} s old; colouring against a '
                'stale tide'.format(age),
                throttle_duration_sec=30.0)
            self._publish_meta('stale_chart_datum')
        else:
            self._publish_meta('ok')

    def _render_pending(self, abort):
        """Render and publish every slippy tile marked dirty.

        Stops early when `abort()` returns True, putting the untouched
        remainder back in the dirty set. Checked before EVERY tile, on every
        path: that is what bounds a pass without depending on how long a
        single upload takes. See `_render_dirty` and SHUTDOWN_FLUSH_SECONDS.
        """
        datum_z = self._datum_offset or 0.0
        with self._lock:
            pending = sorted(self._dirty)
            self._dirty.clear()
        if not pending:
            return

        published = 0
        retry = set()
        for position, (x, y) in enumerate(pending):
            if abort():
                retry.update(pending[position:])
                self.get_logger().warn(
                    'render pass stopped with {} tile(s) left'.format(
                        len(pending) - position))
                break
            try:
                outcome = self._render_one(x, y, datum_z)
            except Exception as error:
                # This runs on a timer. An exception here does not skip a
                # tile, it ends the timer and the node keeps spinning with
                # nothing rendering -- silently, because the subscriptions go
                # on working. Contain it per tile.
                self._note_failure()
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
            # `_tiles` belongs to the executor thread; its size is read under
            # the lock like every other access to it, log line or not. An
            # unlocked read here is atomic in CPython and would only cost a
            # slightly-wrong number -- but it is a counterexample to an
            # invariant the rest of this file depends on being absolute.
            with self._lock:
                cached = len(self._tiles)
            self.get_logger().info(
                'rendered {} tile(s) at z{} ({} total, {} tiles cached)'.format(
                    published, self.zoom, self._rendered, cached))

    def _write_local(self, payload, key):
        """Write one object under local_dir; return True on success.

        Atomic, because the preview directory is served straight off disk by
        a plain http.server that a viewer polls: it must never read a
        half-written PNG. mkstemp creates 0600, which the sibling artifacts
        are not and which a static server may refuse to serve, so the mode is
        set explicitly. A failed write removes its temp file rather than
        leaving it in a directory that is being served.
        """
        path = os.path.realpath(os.path.join(self.local_dir, key))
        root = os.path.realpath(self.local_dir)
        if not (path == root or path.startswith(root + os.sep)):
            # Belt and braces: the prefix is validated at startup, but a
            # traversal here would write outside the preview directory.
            self.get_logger().error(
                'refusing to write {} outside {}'.format(path, root))
            return False
        os.makedirs(os.path.dirname(path), exist_ok=True)
        handle, temporary = tempfile.mkstemp(dir=os.path.dirname(path))
        try:
            with os.fdopen(handle, 'wb') as output:
                output.write(payload)
            os.chmod(temporary, 0o644)
            os.replace(temporary, path)
        except OSError as error:
            self._note_failure()
            self.get_logger().error(
                'writing {} failed: {}'.format(path, error))
            try:
                os.unlink(temporary)
            except OSError:
                pass
            return False
        return True

    def _publish(self, payload, key, content_type='image/png', max_age=None):
        """Write one object locally or to S3; return True on success.

        `max_age` overrides the tiles' `cache_control` for objects that want a
        different one -- the manifest, whose whole job is to be current.
        """
        if self.dry_run:
            return self._write_local(payload, key)

        if max_age is None:
            max_age = self.cache_control
        ok, exc = self._uploader.put(
            payload, key, content_type, 'max-age={}'.format(max_age))
        if not ok:
            self._note_failure()
            self.get_logger().error('upload of {} failed: {}'.format(
                key, describe_error(exc)))
            return False
        return True


def main(args=None):
    """Spin the coverage renderer until interrupted."""
    rclpy.init(args=args)
    node = None
    try:
        # Construction inside the try: a parameter or TF failure in the
        # constructor would otherwise skip the finally entirely and leave
        # rclpy initialised.
        node = CoverageRenderer()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.stop()
            node.get_logger().info(
                'stopping: {} tiles rendered, {} cached, {} failures'.format(
                    node._rendered, len(node._tiles), node._failures))
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
