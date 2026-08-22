# marine_web_view

Public, read-only web view of live vessel state: a ROS 2 node that renders
position and heading to static GeoJSON in an S3 bucket, plus the static page
that displays it.

One-way by construction. The vessel writes files; viewers read them through a
CDN. No ROS surface is exposed to the internet and the page has no server side,
so it is indifferent to viewer count.

Part of [#333](https://github.com/rolker/unh_marine_autonomy/issues/333); this
package is [#341](https://github.com/rolker/unh_marine_autonomy/issues/341).

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

Single-page Leaflet view. Esri World Imagery basemap with CCOM/JHC bathymetry
rendered as fixed-scale depth colours plus hillshade, and the vessel drawn with
CAMP's geometry — triangle while metres-per-pixel exceeds
`max(length, width) / 10`, hull outline below that, circle when heading is
unknown.

Notes that are easy to get wrong and are commented at the point of use:

- The bathymetry rendering rule must be **explicit and fixed-range**. The
  pre-styled `*_DRA` services apply Dynamic Range Adjustment per request, so
  each tile gets its own contrast curve and the map becomes a patchwork.
- `interpolation=RSP_NearestNeighbor` for the banded colours, **bilinear** for
  the hillshade.
- Esri World Imagery is licensed under the Esri Master License Agreement and
  its item states it is not intended for exporting tiles offline: it must stay
  proxied live and must never be cached into our bucket.

## `scripts/refresh_chart_tiles.py`

Pre-renders the CCOM/JHC bathymetry into static tiles in the bucket, so viewer
pans do not render on `gis.ccom.unh.edu`. Detects a new compilation by service
name, validates that the chosen service actually returns data before crawling,
rate-limits, and refuses to publish a partial pyramid. See
[#342](https://github.com/rolker/unh_marine_autonomy/issues/342).

Its `RAMP` / `MAX_DEPTH` / `STEP` must stay in sync with `web/index.html`; the
rule is hashed into `tiles/manifest.json` so a change forces a re-render.
