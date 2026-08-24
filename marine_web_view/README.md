# marine_web_view

Public, read-only web view of live vessel state: a ROS 2 node that renders
position and heading to static GeoJSON in an S3 bucket, plus the static page
that displays it.

One-way by construction. The vessel writes files; viewers read them through a
CDN. No ROS surface is exposed to the internet and the page has no server side,
so it is indifferent to viewer count.

Part of [#333](https://github.com/rolker/unh_marine_autonomy/issues/333); this
package is [#341](https://github.com/rolker/unh_marine_autonomy/issues/341).

## Runtime prerequisite: the AWS CLI

Both nodes upload by shelling out to `aws s3 cp`, so the **AWS CLI v2 must be
installed and a profile configured** on any host that publishes to the bucket.
It is not a package dependency and cannot be: the `awscli` rosdep key resolves
to an apt package with no installation candidate on Ubuntu noble, so declaring
it aborts the build at `rosdep install` — and the CLI in use is the userland
v2 installer, which apt could not provide anyway. Install it per
[AWS's instructions](https://docs.aws.amazon.com/cli/latest/userguide/getting-started-install.html)
and check with `aws --version`.

Nothing else needs it. With `dry_run:=true` both nodes write to the local
filesystem and never call `aws` at all, which is how the tests and the
simulator workflow run.

## Node: `state_renderer`

### Subscribed topics

| Topic | Type | Purpose |
|---|---|---|
| `/marine/platforms` | `marine_interfaces/msg/PlatformList` | Hull dimensions **and** nav topic names |
| *(discovered)* `<ns>/<position_topic>` | `sensor_msgs/msg/NavSatFix` | Position |
| *(discovered)* `<ns>/<orientation_topic>` | `sensor_msgs/msg/Imu` | Heading |

The node is told a **platform name**, not a set of topics. Hull dimensions and
the nav topic names both come from the matching `PlatformList` entry, so the
same node serves any vessel in the fleet unchanged. Every `nav_source` a
platform advertises is followed and the newest message wins, matching CAMP's
`Platform::shape()`.

`PlatformList` is `VOLATILE`, not latched, so the `vessel_*` and `topic`
parameters act as a fallback until the first message arrives — and for running
against something that publishes no `PlatformList` at all.

### Parameters

| Parameter | Default | Notes |
|---|---|---|
| `platform_name` | *(first segment of `topic`)* | Which platform to follow |
| `topic` | `/ben/sensors/nav/position` | Fallback position topic |
| `msg_type` | `navsatfix` | Or `geopoint` for `geographic_msgs/GeoPointStamped` |
| `orientation_topic` | *(empty)* | Fallback; normally discovered |
| `platforms_topic` | `/marine/platforms` | |
| `interval` | `1.0` | Seconds between uploads — see Cost |
| `bucket` / `key` | `unh-ccom-p11-live` / `live/position.geojson` | |
| `profile` | `p11-renderer` | AWS profile; scoped to `s3:PutObject` on `live/*` |
| `dry_run` | `false` | Write to `local_path` instead of S3 |
| `local_path` | `/tmp/position.geojson` | |
| `vessel_length` / `vessel_width` / `reference_x` / `reference_y` | BEN's hull | Fallback only |

### Output

A GeoJSON `Feature` with `heading`, `stamp`, `fix_status`, and the `vessel`
block the page uses to draw the hull. Coordinates are `[longitude, latitude]`
per the GeoJSON spec — the reverse of Leaflet's `L.marker([lat, lng])`.

### Heading

ROS uses ENU (REP-103), so yaw is counter-clockwise from **east** while a
compass heading is clockwise from **north**: `heading = 90 - yaw`. Verified in
simulation against course made good.

## Cost

S3 PUTs are billed per request regardless of object size, so the **upload
interval** is the cost lever — not the message rate. The node keeps the newest
fix and uploads on a timer.

| Interval | PUTs/month (continuous) | Cost |
|---|---|---|
| 1 Hz | ~2.6M | ~$13 |
| 5 s | ~520k | ~$2.60 |
| 10 s | ~260k | ~$1.30 |

Those are **ceilings for 24/7 operation**. The node uploads only when the fix
stamp has advanced, so an idle or disconnected vessel costs nothing: a 40-hour
survey week at 1 Hz is roughly 144k PUTs, about $0.72.

Freshness comes from `Cache-Control: max-age` on the object. CloudFront
invalidation is deliberately **not** used — it is billed per path beyond a small
monthly allowance and would dominate every other cost at any real update rate.

## Running

```bash
# Local, no AWS. Write into web/live/ -- the same prefix the bucket uses, so
# the page's relative URLs work identically here and when deployed.
ros2 launch marine_web_view state_renderer_launch.py dry_run:=true \
    local_path:=/path/to/web/live/position.geojson \
    track_local_path:=/path/to/web/live/track.geojson

# Against S3
ros2 launch marine_web_view state_renderer_launch.py platform_name:=ben
```

Against the simulator:

```bash
ros2 launch marine_simulation sim_robot_launch.py \
    namespace:=ben platform:=bizzy enable_bridge:=false
```

## `web/index.html`

Single-page Leaflet view. Layers, bottom to top: Esri World Imagery, CCOM/JHC
bathymetry rendered as fixed-scale depth colours, hillshade, live sonar
coverage, then the vessel track and hull. The vessel is drawn with CAMP's
geometry — triangle while metres-per-pixel exceeds `max(length, width) / 10`,
hull outline below that, circle when heading is unknown.

Notes that are easy to get wrong and are commented at the point of use:

- The bathymetry rendering rule must be **explicit and fixed-range**. The
  pre-styled `*_DRA` services apply Dynamic Range Adjustment per request, so
  each tile gets its own contrast curve and the map becomes a patchwork.
- `interpolation=RSP_NearestNeighbor` for the banded colours, **bilinear** for
  the hillshade.
- Esri World Imagery is licensed under the Esri Master License Agreement and
  its item states it is not intended for exporting tiles offline: it must stay
  proxied live and must never be cached into our bucket.

## Node: `coverage_renderer`

Renders live sonar coverage to slippy-map PNGs, consuming the ADR-0008 display
transport. Read-only against that transport: it never writes back to the
durable stores, which the display projection is always rebuildable from.

Part of [#345](https://github.com/rolker/unh_marine_autonomy/issues/345).

### The protocol, and why each part matters

| topic | type | QoS | role |
|---|---|---|---|
| `<ns>/coverage_catalog` | `marine_interfaces/TileCatalog` | RELIABLE, **TRANSIENT_LOCAL** | complete snapshot of what the source holds |
| `<ns>/coverage_requests` | `marine_interfaces/TileRequest` | RELIABLE, VOLATILE | what we are missing or stale on |
| `<ns>/coverage_tiles` | `marine_interfaces/SonarVisualizationTile` | **BEST_EFFORT**, VOLATILE | dirty sub-windows |

The catalog is a **complete** snapshot, and that completeness is a
precondition: prune-on-absence against a partial or paged catalog would read
as "the source dropped everything I cannot see".

Two rules keep reconciliation safe, and both fail *silently* when broken --
the display just goes quietly wrong:

- **Newest-wins** (D3): a reordered older arrival must not lower the held
  version, or a stale tile overwrites a fresh one.
- **Timestamp-gated prune** (D4b): a held tile absent from the catalog is
  pruned only if its version predates the catalog's generation time, so a late
  catalog cannot delete a tile pushed after it was generated.

A consequence worth knowing: at generation time zero the gate disables pruning
entirely, because `version < generation` is never true. That is right under
simulated time starting at zero -- better to keep a tile the source may still
hold than delete on no evidence -- but it is emergent from the comparison, not
an explicit branch, so `test_reconciler.py` pins it.

Requests are **batched**. A cold start against a large store wants the whole
catalog, and `coverage_requests` is a shared fanout — the source serves every
resident index in one callback, so an unbounded request is an unbounded burst
on the producer and on every other consumer's tile topic. The remainder is
carried to the next `request_interval`, and each catalog round drops whatever
has arrived, so the tail is reached rather than starved.

Live push is **best-effort**. A lost or malformed patch is dropped and healed
by the next catalog round, which re-requests the tile in full.

That healing only works if *possession* means what it says. A dirty sub-window
updates the pixels but does **not** record possession of the tile: recording it
would let a single lost window become permanent, because the cache would then
claim the catalog's newest version while holding a hole nothing re-requests.
Only a whole-tile message advances the held version. The producer serves every
`TileRequest` as a whole tile (`quantize_tile.cpp` sets the window to the full
grid), so this costs nothing today and keeps the guarantee if sub-window pushes
ever arrive. Ordering is enforced *before* the patch lands, not after: a
reordered stale window is dropped rather than overwriting fresh cells behind
the catalog's back.

### Parameters

| Parameter | Default | Notes |
|---|---|---|
| `coverage_namespace` | `/ben/sensors/mbes/cube_bathymetry` | where the coverage triple lives |
| `band` | `depth` | which `VisualizationBand` to render |
| `zoom` | `15` | slippy zoom, 0-22; higher means more tiles and more PUTs per dirty GGGS tile. Anything outside the range falls back to 15 with a warning -- a negative zoom would otherwise kill the node on the first tile |
| `render_interval` | `20.0` | seconds between render passes |
| `request_interval` | `5.0` | seconds between `TileRequest` publications |
| `bucket` / `prefix` | `unh-ccom-p11-live` / `live/coverage` | |
| `profile` | `p11-renderer` | scoped to `s3:PutObject` on `live/*` |
| `cache_control` | `20` | `max-age` stamped on each object; matched to `render_interval` so a viewer does not hold a tile past its replacement |
| `cache_budget_bytes` | `536870912` | resident tile-cache ceiling (512 MiB); `0` disables the bound |
| `max_requests_per_message` | `256` | tiles asked for per `TileRequest`; the rest wait for the next interval |
| `map_frame` | `ben/map` | frame the band's z values are expressed in |
| `chart_datum_frame` | `ben/chart_datum` | vertical reference for colour; empty disables the correction |
| `tide_invalidate_threshold` | `0.15` | metres of tide change that force a re-render |
| `dry_run` / `local_dir` | `false` / `/tmp/coverage` | write PNGs locally instead of S3 |

Every parameter is exposed by the launch file, and `test_launch_params.py`
enforces that -- a node parameter its launch file does not forward is silently
ignored, which is how `state_renderer` shipped with a documented-but-dead
`track_local_path`.

### Band decoding

Bands are quantized and self-describing: `value = raw * scale + offset`, with
the element type taken from `dtype`. Depth is INT16 but uncertainty,
backscatter and intensity are UINT8, and UINT16 is reserved for sidescan --
hardcoding int16 would mis-decode three of the four into plausible garbage.

`nodata` is a **raw sentinel compared before dequantization**. -32768 raw at
scale 0.01 dequantizes to -327.68, a perfectly plausible depth: comparing
afterwards would render "no data" as very deep water.

### Vertical reference

The `depth` band does **not** carry depth below chart datum. It carries z in
the **map frame**, which is ellipsoidal. Over the Piscataqua that reads about
-36 to -57 m: every value saturates a 0-40 m ramp, and essentially all
coverage paints the deepest colour — which reads as a plausible-looking deep
channel rather than as a bug. Referenced to chart datum the same water is 8 to
29 m, on scale and agreeing with the basemap.

The offset is the **tide**, so it moves through a survey and cannot be a
constant. It is read from `map_frame` → `chart_datum_frame` in TF, the same
way `s57_layer.cpp` reads its own tide offset, and re-read on every render
pass; a change larger than `tide_invalidate_threshold` re-renders the whole
mosaic so the colours track the tide, while smaller TF jitter does not
re-upload it. Depth below datum is `datum_z - value`.

Until the transform is available the renderer **does not render**: colouring
from an unreferenced height would be wrong in a way that looks right. Set
`chart_datum_frame` to the empty string to say the band is already referenced
and skip the correction entirely.

### Cache budget

A GGGS grid is 960 × 960 float32 — 3.69 MB, about 19.6 MB per square
kilometre surveyed — so an unbounded cache grows for as long as the survey
does. `cache_budget_bytes` (512 MiB, the figure CAMP shipped for the same
cache) bounds it, evicting least-recently-updated tiles.

Eviction drops possession with the tile. Unlike CAMP's layer this consumer
has no disk to fall back on, so keeping possession of a tile it no longer
holds would let a later sub-window patch rebuild it from NaN and blank the
coverage already published for it; dropping it means the next catalog round
re-requests the tile in full. The already-published PNG stands until then, so
an evicted tile is not marked dirty and the display does not flicker. Being
over budget is abnormal and is warned about.

### Threading

Rendering runs on its own thread; the `render_interval` timer only wakes it.
A pass samples, PNG-encodes and uploads — with a 30 s timeout per object — so
running it in a timer callback blocks the single-threaded executor for the
whole pass, and every `BEST_EFFORT` tile the source pushes meanwhile is
dropped. Cached tile arrays are replaced rather than patched in place, so the
render thread always samples an immutable array.

Shutdown flushes whatever is still dirty. Without that, up to one render
interval of the end of a survey line is silently lost — the part an operator
is most likely to be looking for.

### Memory-only, by design

There is no warm load and no disk cache. ADR-0008 D5 provides for one — CAMP
uses it, because a GUI has to redraw the whole survey the moment it opens —
but this renderer's durable output *is* the bucket: a restart re-requests what
the source still holds through the ordinary catalog round, and every tile it
had already published is still standing in S3 meanwhile. Adding a disk cache
would buy a faster first pass and a second copy of state to keep consistent.

The one thing this costs is noted under Publishing: PNGs from a *previous*
run at a different zoom or prefix are not tracked, so they are not cleaned up.

### The manifest

Each render pass writes `<prefix>/meta.json` — the zoom, the band, the tile
counts, the chart-datum offset, and a stamp. `stamp` is **wall clock**
(`ros_stamp` carries the ROS clock beside it): the page measures liveness
against `Date.now()`, and under `use_sim_time` a ROS stamp would report a
running renderer as permanently stale. It is both configuration and
heartbeat:

- the page **builds its coverage layer from the manifest's zoom** rather than
  hardcoding it. The two drifted apart silently once; a layer pinned to the
  wrong native zoom requests tiles that were never written, one 403 per tile
  per pan.
- a missing tile is the normal case for a coverage layer, so the page paints
  every miss transparent (`errorTileUrl`). That also hides total failure as
  calm water. The manifest is what lets the page say `offline` or `stale`
  instead.

It is rewritten every pass, idle or not, which is one extra PUT per
`render_interval` (~130k/month at 20 s, well under a dollar).

### Publishing and un-publishing

A rendered PNG lives in the bucket until it is overwritten, so pruned coverage
has to be *un-published*: when a slippy tile the node has published stops
having any coverage under it, a fully transparent tile is uploaded over it.
Skipping the upload instead leaves the display — and CloudFront — showing
coverage the source no longer holds.

That bookkeeping is in memory, so it covers this run. Objects left by an
earlier run at a different `zoom` or `prefix` are not tracked and are not
cleaned up; give a survey its own prefix, or expire the old one with a bucket
lifecycle rule.

A failed upload leaves its tile dirty and is retried on the next pass rather
than becoming a permanent hole, and a per-tile failure is contained: an
exception escaping the render timer would stop rendering for good while the
subscriptions carried on, which looks like a healthy node.

### Rendering

Uncovered cells stay NaN and render transparent. That is the point -- the
shape of the surveyed swath is what a coverage layer carries, so it must not
fill.

Colour reuses the basemap ramp over a fixed 0-40 m scale
([#342](https://github.com/rolker/unh_marine_autonomy/issues/342)) so coverage
and bathymetry read on one scale, with a small tint offset so the layers stay
distinguishable where they overlap. Per the plan this is a recorded **ADR-0001
interim deviation**: `marine_colormap` is the mandated single source of truth
but is C++-only today, so it cannot be called from this node. Expiry: adopt it
once a Python binding exists
([#349](https://github.com/rolker/unh_marine_autonomy/issues/349)).

### Running

```bash
# Against the simulator, writing PNGs locally.
# local_dir is the WEB ROOT, not the coverage directory: the node writes
# <local_dir>/<prefix>/<z>/<x>/<y>.png, so pointing it at the web root with
# the default prefix lands tiles exactly where the page requests them.
# (A prefix that scrubs to empty -- `""`, `"/"`, `"../.."` -- is refused at
#  startup and replaced with the default: an empty prefix makes every key
#  absolute, which would discard local_dir and fail every write.)
ros2 launch marine_web_view coverage_renderer_launch.py \
    dry_run:=true local_dir:=/path/to/web

# Against S3
ros2 launch marine_web_view coverage_renderer_launch.py
```

The simulator is a complete source -- launch it, and `cube_bathymetry`
publishes a live catalog as the vessel surveys:

```bash
ros2 launch marine_simulation sim_robot_launch.py \
    namespace:=ben platform:=bizzy enable_bridge:=false tide_speed_factor:=1
```

`tide_speed_factor` defaults to **10**, compressing a 12.4-hour cycle into
about 74 minutes. A vessel that enters on a high tide can strand minutes
later on charted ground that was navigable when it arrived -- realistic
behaviour at an unrealistic rate. Use `1` for sustained work.

## `scripts/refresh_chart_tiles.py`

Pre-renders the CCOM/JHC bathymetry into static tiles in the bucket, so viewer
pans do not render on `gis.ccom.unh.edu`. Detects a new compilation by service
name, validates that the chosen service actually returns data before crawling,
rate-limits, and refuses to publish a partial pyramid. See
[#342](https://github.com/rolker/unh_marine_autonomy/issues/342).

Its `RAMP` / `MAX_DEPTH` / `STEP` must stay in sync with `web/index.html`; the
rule is hashed into `tiles/manifest.json` so a change forces a re-render, and
`test/test_ramp_sync.py` fails if the Python and JS copies ever diverge.
