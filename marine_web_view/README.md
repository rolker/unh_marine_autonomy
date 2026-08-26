# marine_web_view

Public, read-only web view of live vessel state: a ROS 2 node that renders
position and heading to static GeoJSON in an S3 bucket, plus the static page
that displays it.

One-way by construction. The vessel writes files; viewers read them through a
CDN. No ROS surface is exposed to the internet and the page has no server side,
so it is indifferent to viewer count.

Part of [#333](https://github.com/rolker/unh_marine_autonomy/issues/333); this
package is [#341](https://github.com/rolker/unh_marine_autonomy/issues/341).

## AWS credentials

Uploads go through `boto3`, declared as `python3-boto3` in `package.xml`, so
`rosdep install` satisfies the package's own dependency — no separate CLI
install ([#351](https://github.com/rolker/unh_marine_autonomy/issues/351)).
A host that publishes to the bucket still needs **credentials**: the `profile`
parameter names an entry in `~/.aws/credentials`. `coverage_renderer` treats an
empty `profile` as "use boto3's default chain", which is how an EC2 instance
role is picked up with no key on disk; `state_renderer` passes its `profile`
through unchanged, so blanking it there fails loudly rather than quietly
borrowing whatever the host carries.

With `dry_run:=true` both nodes write to the local filesystem and reach for no
credentials at all — no client is constructed — which is how the tests and
the simulator workflow run.

### What happens when S3 is slow

There is no per-upload time ceiling to quote, and nothing here is built on
one: `connect_timeout` is applied per DNS address and the S3 endpoint resolves
to several, so a single PUT has no useful worst case. Both nodes are arranged
so that does not matter.

- **`state_renderer` never uploads on the thread that receives position
  fixes.** Payloads go to a background worker that keeps only the **newest**
  object per key. While the endpoint is slow, superseded positions are
  dropped rather than queued — the artifact is a snapshot of the present, so
  the operator sees the current position as soon as an upload gets through,
  not a march of stale ones. Fixes keep being recorded throughout, so the
  track has no hole. On shutdown the worker is given a few seconds and then
  abandoned; at a 1 s cadence the unsent last position is worth less than a
  clean exit.
- **`coverage_renderer` checks for a stop before every tile it uploads**, so
  Ctrl-C takes effect without waiting for an upload already in flight. It
  then flushes whatever is still dirty under a 30 s deadline (a coverage tile
  is *not* superseded by the next one — drop it and that patch of seabed is
  missing until its grid changes again). If the render thread is genuinely
  inside a request when the stop arrives, the flush is skipped and logged:
  `render thread still inside a request after 10 s`.

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
| `track_key` | `live/track.geojson` | Bucket key for the served track history |
| `track_local_path` | `/tmp/track.geojson` | Where the track is written under `dry_run`; point it into `web/live/` to preview the trail |
| `track_seconds` | `14400.0` | How much history the track carries (4 h) |
| `track_max_points` | `1200` | Hard cap on track vertices — a safety net after band decimation; hitting it trims the OLDEST fixes and warns |
| `track_interval` | `30.0` | Seconds between track uploads — slower than `interval`, which is why the page bridges the gap with locally observed fixes |
| `vessel_length` / `vessel_width` / `reference_x` / `reference_y` | BEN's hull | Fallback only |

### Output

A GeoJSON `Feature` with `heading`, `stamp`, `fix_status`, and the `vessel`
block the page uses to draw the hull. Coordinates are `[longitude, latitude]`
per the GeoJSON spec — the reverse of Leaflet's `L.marker([lat, lng])`.

### Heading

ROS uses ENU (REP-103), so yaw is counter-clockwise from **east** while a
compass heading is clockwise from **north**: `heading = 90 - yaw`. Verified in
simulation against course made good.

## Node: `ais_renderer`

Renders the AIS traffic around the vessel to one static GeoJSON object, so the
public page shows BizzyBoat in context rather than alone on empty water.

### Subscribed topics

| Topic | Type | Purpose |
|---|---|---|
| `/ais/contacts` | `marine_ais_msgs/msg/AISContact` | Tracked contacts, one message per position report |

Published ashore, on the ROC desktop, by the `nmea_relay` →  `ais_parser` →
`ais_contact_tracker` chain — the receiver is a shore station, not the boat.
`ais_contact_tracker` republishes the *whole* accumulated contact (position
report plus whatever static and voyage data it has seen) on every position
report, so the newest message per MMSI is the complete state of that contact
and there is no out-of-order history to reconcile.

### Parameters

| Parameter | Default | Notes |
|---|---|---|
| `use_sim_time` | `false` | The ROS builtin. **Required for bag replay** — see [Running](#running) |
| `contacts_topic` | `/ais/contacts` | The tracker publishes at the global namespace, not under the operator namespace |
| `interval` | `10.0` | Seconds between uploads — see Cost. AIS does not need the position topic's 1 Hz |
| `bucket` / `key` | `unh-ccom-p11-live` / `live/ais.geojson` | |
| `profile` | `p11-renderer` | AWS profile; scoped to `s3:PutObject` on `live/*` |
| `dry_run` | `false` | Write to `local_path` instead of S3 |
| `local_path` | `/tmp/ais.geojson` | Point it into `web/live/` to preview the layer |
| `contact_timeout` | `600.0` | Drop a contact unheard from for this long, from the snapshot **and** from memory |
| `max_contacts` | `500` | Ceiling on contacts held at once — bounds object size and CDN egress, which the PUT count does not |
| `heading_variance_threshold` | `1.0e5` | Yaw variance at or above which no true heading was reported |

### Output

One GeoJSON `FeatureCollection` for **all** contacts, not one object each —
minus the contacts [withheld from the public page](#who-is-not-on-the-map). Per
contact: `mmsi`, `name`, `callsign`, `ship_and_cargo_type`,
`navigational_status`, `speed_knots`, `course_deg`, `heading_deg`, `stamp`,
`stamp_iso`, and the same `vessel` block `state_renderer` publishes, so the
page draws AIS contacts through the identical `hullShape()` it draws BizzyBoat
with. `name` and `callsign` are `null` until a type 5 or 24 message has been
heard — the ordinary case for the first minutes after a contact comes into
range, and what the page renders as *static info pending*. Both are scrubbed
of control and bidi characters before they are published: the popup is built
from text nodes so markup is already closed off, but a bidi override reorders
the text around it, and an unpaired surrogate makes the whole object
unparseable for every viewer at once.

Uploads happen only when the contact set or its contents change — and a
contact's `stamp` counts as its contents, because the page reads that stamp as
the layer's liveness signal (see below). So an **empty** river costs nothing,
while a river with anything at all on it costs up to one PUT per `interval`,
which is the ceiling the Cost section quotes. `generated` in the
collection's properties is therefore when the artifact was last **rebuilt**,
not the last tick: an unchanging value means "nothing has changed since", not
necessarily "the renderer is alive". Liveness comes from each feature's own
`stamp` instead — the page fades a contact unheard for more than 5 minutes and
reports the freshest contact's age in the readout, so a dead renderer (or a
receiver that lost its antenna) is visibly different from a quiet river rather
than a confident fleet of hulls that never moves again.

### Expiry

`contact_timeout` defaults to 10 minutes — deliberately generous, and well
above `ais_layer`'s own 90–180 s class-A/B costmap timeouts. Those protect
motion planning and must react fast to a contact going stale; this is a
human-facing display, where a marker flapping out and back on every reporting
gap is worse than a slightly stale one, and a shore receiver cannot distinguish
a contact that departed from one that dropped below VHF range. An expired
contact is pruned from memory, not merely filtered out of the snapshot, so an
MMSI that ages out and later reappears is a fresh entry with no stale state
behind it.

`max_contacts` is the other ceiling, and it bounds a different cost. The PUT
count does not scale with contact count — one object per `interval` either way
— but the object's **size** does, and so does the per-viewer CDN egress that
follows it, over an MMSI keyspace that is an unauthenticated `uint32` on the
air. At the ceiling a new MMSI evicts the oldest contact held, which is the one
closest to expiring anyway, and says so in the log: past that point the
artifact is a truncated picture of the river, and that is worth knowing. The
default is well above what a shore receiver on the Piscataqua hears.

### Who is not on the map

Two categories of contact are dropped before they reach the artifact, and the
filter runs in the **renderer**, not on the page — so those positions never
reach S3 or the CDN at all:

| Withheld | Code |
|---|---|
| Distress beacons | navigational status `14` — AIS-SART / MOB / EPIRB |
| Search and rescue, law enforcement | ship-and-cargo types `51` and `55`, **and** AIS message `9`, the SAR aircraft position report |

Each is tested on the identifier the standard gives it rather than on the most
convenient field. A SAR aircraft carries neither of the first two: message 9
has no navigational status field at all, and an aircraft sends no type 5 or 24
static report, so its `ship_and_cargo_type` stays `0` — "not applicable to SAR
aircraft," in the standard's own words. The message id is the whole of its
identity, and it is the field `ais_layer` switches on for the same contact.

The page exists to show BizzyBoat in context, so it has no operational need for
either. AIS being public data elsewhere is not a reason for *this* page to
rebroadcast a person in the water: an AIS-SART is somebody's worst day, and a
CDN-fronted map of it is a different act from a receiver hearing it. A
responder's own position is withheld for the neighbouring reason — it says
where a response is happening, and by inference what it is responding to, while
it is happening.

Filtering on the page instead would not do this. A hidden marker is still a
position in the bucket, on the CDN, and in the page source one click away; the
only version that keeps the data off a public network is the one that never
uploads it. That also means a contact already published is *withdrawn* when it
later declares distress or resolves to type 51/55 — the filter runs on every
report, not only the first.

Each exclusion is logged once per MMSI on the ROC desktop. A filtered page and
an empty river produce byte-identical artifacts, and that log is the only thing
that can tell them apart.

### Two other things that are invisible in a good run

**A contact with no position.** `ais_parser` writes NaN lat/lon when a position
report carries none, and the tracker copies the pose and publishes it anyway.
`json.dumps` renders that as a bare `NaN` token, which `JSON.parse` rejects
outright — so one silent beacon would blank the AIS layer for **every** viewer,
not degrade one marker. Such contacts are dropped (logged once per MMSI, not
per message) and `allow_nan=False` sits behind that as a backstop.

**"No heading" versus "heading due east."** An unreported AIS true heading
leaves an identity or null quaternion behind, and the identity is a perfectly
valid yaw of zero — due east in ENU. `ais_contact_tracker` records what was
actually reported in the yaw covariance, so that is what is tested here, the
same way `ais_layer.cpp` does for the costmap. `heading_variance_threshold` is
a decade below the tracker's `unknown_variance` sentinel (1e5 against 1e6) so
the sentinel still reads as unknown if the two configurations drift.

`course_deg` is `null` for a stationary contact: `ais_parser` encodes course
over ground as the *direction* of the velocity vector, so at a speed of zero
the course is not recoverable and reporting `atan2(0, 0)` would publish a
confident due east.

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

There are now three PUT streams against the same bucket, each with its own
interval and its own "only on new data" gate: `state_renderer`'s position and
track, `coverage_renderer`'s tiles and manifest, and `ais_renderer`'s contact
snapshot. AIS is the cheapest of the three by construction — one object at
`interval` (10 s → ~260k PUTs/month, ~$1.30 as a 24/7 ceiling), and only when
the contacts actually changed. Its cost does **not** scale with contact count:
a busy river and a river with one vessel on it cost the same number of PUTs,
and a river with nothing on it costs none. What it *does* scale with is
whether anything is being heard at all, because a contact re-heard carries a
new `stamp` and the page needs that stamp to advance to know the renderer is
alive — the ceiling above is the price of that.

GETs are the other side, and they scale with **viewers**, not with the
renderers. Each open page fetches the position once a second, the track every
30 s, the coverage manifest every poll and the AIS snapshot every 10 s. That is
what `Cache-Control: max-age` bounds: the CDN answers most of it from the edge,
so origin GETs stay a function of the interval rather than of how many people
are watching. AIS adds ~6 origin GETs/minute at the edge worst case, on the
order of cents a month — the reason the page polls it on its own slower timer
rather than on the 1 Hz position tick.

Freshness comes from `Cache-Control: max-age` on the object — per object,
not one value for the bucket: the tiles carry `cache_control` and the liveness
manifest carries a shorter one of its own (see [The manifest](#the-manifest)).
CloudFront invalidation is deliberately **not** used — it is billed per path beyond a small
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

The AIS renderer runs where the AIS receiver is — ashore, on the ROC desktop,
alongside `ais_contact_tracker`:

```bash
# Local, no AWS.
ros2 launch marine_web_view ais_renderer_launch.py dry_run:=true \
    local_path:=/path/to/web/live/ais.geojson

# Against S3
ros2 launch marine_web_view ais_renderer_launch.py
```

Waiting for a ship to come up the river is not a test. Both replayable inputs
work and neither needs the receiver:

```bash
# A recording of the tracker's own output, which is what the node subscribes to.
ros2 bag record /ais/contacts        # once, while the receiver is up

# Thereafter. --clock and use_sim_time are BOTH required, and the failure if
# either is missing is silent -- see below.
ros2 bag play <bag> --clock
ros2 launch marine_web_view ais_renderer_launch.py dry_run:=true \
    use_sim_time:=true local_path:=/path/to/web/live/ais.geojson

# Or from further upstream, which also exercises the decode: replay a recorded
# NMEA log into ais_parser and let ais_contact_tracker build the contacts. The
# same clock rule applies to every node in the chain.
```

**Why the clock matters here.** Contact age is the difference between a
contact's header stamp — recorded when the shore receiver heard it — and the
node's own clock. Replay a bag into a renderer left on the wall clock and every
stamp is as old as the recording, so `contact_timeout` expires the whole set on
the first tick and the artifact publishes `contacts: 0`. Nothing errors; the
page is simply empty. `ros2 bag play --clock` publishes the recorded time on
`/clock` and `use_sim_time:=true` makes the renderer read it, which puts both
sides of that subtraction back on the same clock. The node also warns, once,
when it receives a contact that is already older than `contact_timeout` —
that message is this mistake.

Expiry is the one behaviour a short replay will not show on its own — drop
`contact_timeout` to a few seconds to watch contacts age out, or read
`test_ais_renderer.py`, which pins it against a fixed clock.

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

AIS contacts are drawn on top, in their own layer group, from
`live/ais.geojson` on a slower poll of its own — through the *same*
`hullShape()`, so they inherit the identical zoom-dependent
triangle/circle/hull rule the vessel has. The **AIS** checkbox in the readout
hides and shows the group; the poll continues either way, so toggling costs
nothing and re-showing is instant. Contacts are added, updated and removed by
MMSI rather than rebuilt, so an open popup survives the poll — the layer
redraws every 10 s and on every zoom, and rebuilding took the popup with it.
Each contact carries a popup: name (or
*static info pending* until a type 5 or 24 message has been heard), MMSI, call
sign, ship and cargo type, navigational status, speed, course, heading, and
how long ago it was last heard.

**Staleness.** S3 serves the last AIS artifact with a cheerful `200` no matter
what happened upstream, so the page has to decide liveness for itself. It does
so from each contact's own `stamp`: past `AIS_STALE_S` (300 s — ITU reporting
intervals top out at 3 minutes, so five is not "between reports") the hull is
drawn faded, and the **AIS** readout leads with *stale* and the freshest
contact's age. An artifact that cannot be fetched at all — missing, forbidden,
5xx — reads as *offline* and fades everything, rather than passing for an empty
river.

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
| `render_interval` | `20.0` | seconds between render passes; must be positive and at most a day, or it falls back to 20 s with a warning (`create_timer` rejects a non-positive period) |
| `request_interval` | `5.0` | seconds between `TileRequest` publications; validated the same way, falling back to 5 s |
| `bucket` / `prefix` | `unh-ccom-p11-live` / `live/coverage` | an empty or malformed `bucket` **refuses to start** when `dry_run` is false: every upload would be a doomed S3 PUT in a retry loop that never drains, and defaulting instead would publish a survey's coverage somewhere nobody asked for |
| `profile` | `p11-renderer` | scoped to `s3:PutObject` on `live/*`. Empty means "use the default credential chain" — boto3 is handed no profile at all rather than an empty one |
| `cache_control` | `20` | `max-age` stamped on each **tile**; matched to `render_interval` so a viewer does not hold a tile past its replacement. `meta.json` is deliberately not covered by it — see [The manifest](#the-manifest) |
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

`cache_budget_bytes` bounds the *resident generation*, not peak RSS. Patching
is copy-on-write — `_on_tile` replaces a tile's array wholesale so the render
thread can sample outside the lock — and the renderer's snapshot keeps the
superseded copies reachable for the length of a pass, while `_cache_bytes`
counts only the current ones. A pass that touches every cached tile can
therefore peak near **twice** the budget. Size the host for 2× the number you
set, not 1×.

That last guarantee has one hole worth knowing: a slippy tile straddling two
GGGS grids is re-rendered whenever *either* of them changes, and it is
rendered from whatever is in the cache. If one of the two has been evicted,
its half of that tile is re-published transparent — surveyed ground that
blanks until the catalog round re-serves the evicted grid. It is bounded (the
tiles along a grid seam, for one render interval) and self-healing, but it is
why running over budget is a warning rather than routine: the answer is a
larger `cache_budget_bytes` or a coarser `zoom`, not living with eviction.

A tide crossing re-renders everything. When the chart-datum offset moves more
than `tide_invalidate_threshold` (0.15 m — one 8-bit step of the 0–40 m ramp),
every cached tile is marked dirty and the whole mosaic is re-coloured and
re-uploaded. Cost scales with **surveyed area × 4^zoom**: at zoom 15 a tile is
about 870 m across at 43° N, so a 10 km² survey is a few dozen tiles and the
re-render is seconds and pennies. It is not free at every setting — the same
area at zoom 18 is 64× the tiles — so raise the threshold, or coarsen the
zoom, before rendering a large area at a deep level. On a 3 m tide the
threshold is crossed roughly every few minutes near mid-tide.

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
  instead — and, past `COVERAGE_DEAD_S` (120 s), fade the layer to
  `COVERAGE_STALE_OPACITY`. The readout alone is not enough: the map is what
  people look at, and a dead renderer's last upload otherwise keeps rendering
  as a confident, complete mosaic. The layer is dimmed rather than removed —
  the coverage it does show is still the best record of where the vessel has
  been.
- the page **validates the manifest before it uses it**. This is remote JSON
  over the public internet, not configuration: `zoom` must be an integer in
  `0..22` (`typeof x === 'number'` admits `NaN` and `Infinity`, and both flow
  straight into `minZoom`/`minNativeZoom` — the bound that stops Leaflet
  laying the viewport out in millions of native-zoom tiles), and `stamp` must
  be finite (a missing stamp makes the age `NaN`, every staleness comparison
  false, and the panel reports a healthy tile count for a renderer that died
  hours ago). A manifest failing either check is treated exactly like no
  manifest at all.

- `status` says what the pass actually did. `ok` is a normal render;
  `truncated_render` means the pass ran out of budget — the stop event on a
  scheduled pass, the flush deadline on the way out — so some tiles it had
  published are in the bucket while the rest stayed dirty; the manifest is
  published anyway, because it is what tells the page to re-request the tiles
  that *did* land, and on the shutdown flush there is no later pass to do it;
  `waiting_for_chart_datum` means no offset has been read yet and nothing was
  coloured; `stale_chart_datum` means the transform the offset came
  from is more than `DATUM_STALE_SECONDS` (60 s) old and `chart_datum_age`
  says how old. The age is taken from the transform's own stamp, not from when
  the lookup ran: `lookup_transform(..., Time())` returns the *latest
  available* transform and tf2 prunes only on insert, so a tide publisher that
  dies keeps resolving the same transform forever and timing the lookup would
  report a permanently fresh datum. A *static* chart-datum transform has no
  age — tf2 answers a `Time()` query on one with a zero stamp, and a datum
  that never moves cannot go stale. That staleness is the degradation the
  manifest used to hide: the offset is the *tide*, so once the publisher goes
  away the node keeps rendering happily against a frozen water level, and
  frozen tide reads on the page as ordinary bathymetry. Coverage
  still renders — stale is a degradation, not a stop.

- `rendered_tiles` is the **change signal**, and it is the field the page
  refreshes on. It is the running total of tiles this process has published,
  so it does not move on an idle pass. The page needs it because `stamp` moves
  every pass whether anything changed or not: refreshing on a new stamp means
  every viewer tears down and re-requests every tile under the viewport once
  per `render_interval` forever — with the sonar off and the boat docked, and
  billed to us, since the uncovered majority of the viewport answers 4xx with
  no cache headers at all. `published_tiles` cannot substitute: it is the
  *size* of the published set, so it does not move when an already-published
  tile is re-rendered (the common case as a survey line grows inside a tile)
  and it *decreases* when coverage is pruned. A restart resets the counter, so
  the page compares it for **change**, not for growth, and a manifest without
  the field is read as "no change" — the page will not refresh tiles against a
  renderer older than this field, though liveness and the readout still work.

- the manifest is **cached differently from the tiles it advertises**, at
  both ends of the path. It carries `max-age=5` (`META_MAX_AGE_SECONDS`,
  never longer than `cache_control` — an operator who shortens the tiles'
  max-age is asking for a fresher display, not a fresher manifest only) and
  the page fetches it with `cache: 'no-store'`. Stamped and fetched like a
  tile, it is not: the renderer's max-age is matched to `render_interval`,
  which is also the page's poll period, so a poll is routinely answered from
  a copy of the *previous* pass — and then `rendered_tiles` has not moved, so
  the change gate never fires and newly surveyed ground does not appear for a
  stationary viewer, while `stamp` is old, so the age is under-reported and a
  renderer that has just died still reads as alive. Both failures look exactly
  like success. `no-store` is per-request and leaves the shared cache alone; a
  cache-busting query string was rejected because a URL that changes per
  request also defeats the CloudFront edge cache, which is the part worth
  keeping. The 5 s edge max-age is what keeps request volume independent of
  viewer count: 12 origin GETs a minute however many people are watching,
  about 13k/day more than a 20 s max-age would be — on the order of $0.15 a
  month — for a fourfold cut in the worst-case staleness of the one object
  the display's liveness depends on. Zero was rejected for that reason: on a
  public page with unbounded viewers the ceiling is worth more than the last
  five seconds.

It is rewritten every pass, idle or not, which is one extra PUT per
`render_interval` (~130k/month at 20 s, well under a dollar).

### Publishing and un-publishing

A rendered PNG lives in the bucket until it is overwritten, so pruned coverage
has to be *un-published*: when a slippy tile the node has published stops
having any coverage under it, a fully transparent tile is uploaded over it.
Skipping the upload instead leaves the display — and CloudFront — showing
coverage the source no longer holds.

That bookkeeping is in memory, so it covers this run and no other. Two
consequences, and the second is the one that bites:

- objects left by an earlier run at a different `zoom` or `prefix` are not
  tracked and are not cleaned up. Give a survey its own prefix, or expire the
  old one with a bucket lifecycle rule.
- **a restart against the same bucket and prefix starts with an empty
  `_published` set.** Coverage the source pruned while the node was down is
  never un-published: the restarted node has no record of having published
  those tiles, so it renders nothing for them and their old PNGs stand in the
  bucket indefinitely. Anything the source *still* holds is re-requested and
  overwritten by the ordinary catalog round, so this only strands ground that
  was pruned across the outage. A prune is rare enough that carrying a
  persistent index was judged the wrong trade (see Memory-only, by design);
  if it matters for a given deployment, clear the prefix before restarting.

The manifest's `prefix` field is **reporting, not configuration**: the page
hardcodes `COVERAGE_DIR` because it has to know where the manifest is before
it can read the manifest. The field is there so anything reading `meta.json`
out of band — a script, an operator checking which prefix a live renderer is
writing to — does not have to guess.

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

Uploads compare each local tile's MD5 against the S3 object's ETag and skip
only genuine matches — a re-render into the same `--name` prefix after a ramp
change is caught even when the new PNG happens to be the same size. `--rate`
limits requests to CCOM; `--concurrency` (default 10) is the separate S3
upload fan-out. Beyond the stdlib it needs only `boto3`, and nothing from
`marine_web_view`, so it runs from cron without the ROS overlay sourced.

### Operating it from cron

- **`--force` is the only way a cache-policy change reaches an unchanged
  tile.** The MD5/ETag comparison above cannot see `Cache-Control`, and S3's
  listing does not report it, so editing `TILE_EXTRA_ARGS` otherwise reaches
  only tiles whose pixels also moved and leaves the prefix on a permanently
  mixed policy. `--force` skips the comparison and re-PUTs everything (it
  also re-renders regardless of `--max-age-days`, so budget the full crawl).
- **One run per `--name` at a time.** The run lock is taken before the first
  request to CCOM, because two overlapping runs double the request rate
  against their server. It lives in `$XDG_RUNTIME_DIR/p11-tiles/<name>.lock`,
  or `~/.cache/p11-tiles/<name>.lock` for a cron job with no session —
  deliberately **not** in `--workdir`, which defaults under world-writable
  `/tmp` and is also the directory whose contents get PUT to the public
  prefix. Different `--name`s may run together.
- **A locked-out run exits 0 and says so on stderr** (so cron mail does not
  cry wolf on an ordinary overlap), *unless* the holder has been there longer
  than six hours — a full crawl is well under an hour, so that is a wedged
  run, and it exits **1** so it stops reading as success forever. The message
  names the holding pid: **clear it by killing that process**, which is what
  releases the lock — the kernel drops a `flock` when the holder exits. Do
  **not** delete the lock file: `flock` is held on the *inode*, not on the
  name, so unlinking it releases nothing and the next run simply creates a
  fresh inode and acquires immediately — two crawls of CCOM's server at once,
  the one thing the lock exists to prevent. The pid line is written just
  *after* the lock is taken, so a reader that catches that gap sees the
  previous run's line instead; if the named pid is not running, the holder is
  someone else and `fuser <lockfile>` (or `lsof`) names it. A lock file whose
  holder has genuinely exited is not held at all — the next run takes it
  without a word, which is why "the file is still there" is never the
  diagnosis.
- **The upload fan-out has a 3600 s aggregate deadline.** Uploads that have
  not started by then are counted as failures, which withholds the manifest,
  so the run reports failure and the next one redoes the upload. The clock
  starts after the crawl and the MD5 pass, and a request already in flight
  can overrun it — the run lock, not this deadline, is what keeps the next
  cron run out.
- **`tiles/manifest.json` is shared by every `--name`.** It is re-read and
  merged under a lock at the end of a run, so two names running together no
  longer erase each other's entry (which cost the loser a full ~5,839-tile
  re-crawl). That read is strict, unlike the one at the top of a run: a GET
  that *fails* is not a manifest that is *empty*, and merging into an empty
  one would erase the other names just as thoroughly as the race did — so the
  run fails loudly instead, leaving the published manifest untouched — which
  costs *this* name a re-crawl next run, and is the cheaper of the two
  losses. The merged JSON is PUT from memory; nothing is
  staged through `--workdir`. That lock is local to the host; publishing this
  prefix from two hosts at once is not supported.
