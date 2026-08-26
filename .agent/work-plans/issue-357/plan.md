# Plan: AIS layer for the public web view (ais_renderer)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/357

## Context

`marine_web_view` already renders live vessel state (`state_renderer`) and
sonar coverage (`coverage_renderer`) to static GeoJSON/PNG in an S3 bucket,
polled by `web/index.html`. The public page shows only BizzyBoat — no
surrounding traffic. `bizzyboat_project11/operator_core_launch.py` already
runs `nmea_relay → ais_parser → ais_contact_tracker` on pandy (the ROC
desktop, ashore), publishing `marine_ais_msgs/AISContact` at the global
`/ais/contacts` topic (verified in `review-issue`: `ais_launch.py` is
included outside the `operator_namespace` group and pushes only `ais`).

`AisContactTracker` (`marine_ais_tools/ais_contact_tracker.py`) keeps one
`AISContact` per MMSI in memory and republishes it on every position report
(messages 1/2/3/9/18/19), with `static_info`/`voyage` filled in once a type
5/24 message has been seen. `AISContact.static_info` carries the four AIS
reference dimensions (A/B/C/D — distance to bow/stern/port/starboard), which
is exactly the shape `state_renderer`'s `vessel` property already uses
(`length`, `width`, `reference_x`, `reference_y`), and which the page's
existing `hullShape()` already turns into a hull or a fallback triangle/circle
depending on zoom and heading availability.

This plan adds a third renderer, `ais_renderer`, following the same
`bucket`/`key`/`profile`/`dry_run`/`local_path`/`interval` parameter surface
and `AsyncUploader` upload path as `state_renderer`, plus a toggleable AIS
layer on the page that reuses `hullShape()`/`drawBoat()` generalized to many
contacts instead of one.

## Approach

1. **`ais_renderer.py`** — new node in `marine_web_view/marine_web_view/`,
   modeled on `state_renderer.py`'s construction and upload plumbing (same
   `S3Uploader`/`AsyncUploader` import, same dry-run/no-AWS-import gate, same
   `_write_atomic`, same `stop()`/`upload_counts()`/`main()` shape).

   - Subscribe to `marine_ais_msgs/msg/AISContact` on `contacts_topic`
     (default `/ais/contacts`).
   - Keep one `AISContact` per MMSI in a dict, updated on every message
     (newest wins by construction — the tracker republishes the whole
     accumulated contact each time, so there is no out-of-order concern like
     `state_renderer`'s fix history).
   - On a timer (`interval`), build **one** GeoJSON `FeatureCollection`:
     contacts whose age (`now - header.stamp`) is under `contact_timeout` are
     included as `Point` features; older ones are dropped before the
     snapshot is built, both from the page's next view and from the
     in-memory dict (bounded growth — an MMSI that ages out and later
     reappears is just a fresh entry).
     Skip the upload/write entirely if the set of included contacts and their
     included property values are unchanged since the last confirmed write
     (mirrors `state_renderer`'s "publish only on new data" tick, generalized
     from one stamp to a set) — this is the S3-PUT cost lever the issue
     calls out ("S3 PUTs bill per request").
   - Per-contact properties: `mmsi` (`AISContact.id`), `name`/`callsign`
     (from `static_info`, `null` until a type-5/24 message has been seen —
     the issue's "position but no name" case), `ship_and_cargo_type`,
     `navigational_status`, `speed_knots`/`course_deg` (from `twist`, `null`
     when NaN — `ais_contact_tracker` writes NaN for an absent SOG/COG),
     `heading_deg` (`null` unless the heading covariance the tracker fills
     in is below the "unknown" threshold — mirrors `ais_layer.cpp`'s own
     `heading_reported` test, since the quaternion alone cannot distinguish
     "no heading" from "heading is due east"), `stamp`, `stamp_iso`, and a
     `vessel` block (`length`/`width`/`reference_x`/`reference_y`) computed
     from `static_info`'s A/B/C/D fields the same way
     `ais_contact_tracker.calculatePolygon` does, but stopping at the
     dimensions rather than building a polygon (see Geometry decision below).
   - `dry_run` writes to `local_path` via the same atomic
     temp-file-plus-`os.replace` helper `state_renderer` uses.

2. **`ais_renderer_launch.py`** — new launch file in `marine_web_view/launch/`,
   declaring and forwarding every node parameter, matching
   `state_renderer_launch.py`'s shape (this is enforced by
   `test_launch_params.py`).

3. **`web/index.html`** — an AIS layer:

   - Poll a new `AIS_SRC = 'live/ais.geojson'` on its own interval
     (`AIS_MS`), independent of the 1 s position poll — AIS contacts do not
     need 1 Hz refresh.
   - Generalize `hullShape()`'s caller: today `drawBoat()` draws exactly one
     hull (`last`, `hull`). Add a `L.layerGroup` of hull polygons keyed by
     MMSI, rebuilt each poll from the FeatureCollection using the *same*
     `hullShape()` the vessel already uses — reuse, not a parallel
     implementation, and it inherits the existing triangle/circle/hull
     zoom-dependent fallback for free.
   - A checkbox in the HUD (`<label><input type="checkbox" id="ais" checked>
     AIS</label>`, next to `follow`) toggles the layer group's visibility —
     the issue's "toggleable" requirement — without touching the poll
     itself, so unchecking it costs nothing extra.
   - A popup per contact (`bindPopup`, built from the feature properties)
     showing name-or-MMSI, callsign, ship/cargo type, speed, course,
     navigational status, and "static info pending" when `name` is `null`
     (the issue's explicit "position but no name" rendering requirement).
   - `test_page_layers.py`'s `test_expected_layers_are_present` and
     `test_every_vector_layer_reaches_the_map` apply unchanged (hull
     polygons are `L.polygon`, already covered by `_VECTOR_CONSTRUCTIONS`)
     but the new source string (`live/ais.geojson`) should be added to that
     test's marker tuple.

4. **`package.xml`** — add `<depend>marine_ais_msgs</depend>`.

5. **`dependencies.repos`** — add a `marine_ais` entry alongside
   `unh_marine_navigation`, per its own stated scope ("add entries here only
   as concrete CI source-dep gaps surface") — `marine_ais_msgs` is a genuine,
   currently-unlisted CI source-dep gap (flagged in `review-issue`).

6. **`README.md`** — new `## Node: ais_renderer` section (topics,
   parameters, output shape) alongside the existing `state_renderer`/
   `coverage_renderer` sections, plus a row in the `web/index.html` section
   describing the AIS layer and its toggle. Required for
   `test_launch_params.py`'s `test_the_readme_documents_every_node_parameter`.

7. **`test_launch_params.py`** — add `('ais_renderer.py',
   'ais_renderer_launch.py')` to `PAIRS`. This tuple is hand-maintained (not
   self-discovering, unlike `test_page_layers.py`'s layer-class scan), so a
   new renderer that skips this edit passes the suite while shipping an
   unforwarded parameter.

### Decisions this issue calls out

- **Expiry** (`contact_timeout`, default `600.0` s / 10 min). Generous, in
  the spirit of `ais_layer`'s own tuning note (a contact fading out of shore
  VHF range is indistinguishable from one that departed) but well above
  `ais_layer`'s own 90–180 s class-A/B costmap timeouts — those protect
  motion planning and must react fast to a contact going stale; this is a
  human-facing display where flapping (a contact vanishing and reappearing
  every reporting gap) is worse than a slightly stale marker. A contact that
  ages out is simply dropped from the next `FeatureCollection` and the page
  removes its hull/popup on the next poll — no distinct "fading" state, to
  keep the page's contact-diffing logic (add/update/remove by MMSI) the only
  code path.
- **Cadence** (`interval`, default `10.0` s). One `FeatureCollection` object
  regardless of contact count (per the issue), gated on the contact set
  actually changing since the last confirmed write — an idle river with the
  boat on the trailer costs zero PUTs. `AIS_MS` on the page defaults to the
  same 10 s.
- **Geometry**: `vessel` dimensions (length/width/reference_x/reference_y)
  derived from `static_info`'s A/B/C/D, not `AISContact.footprint` directly.
  `footprint` is a body-frame polygon (rotated/translated by heading and
  position only in `ais_layer.cpp`'s C++ costmap code); reusing it on the
  page would mean porting that rotation math into a second, parallel
  implementation. The A/B/C/D-derived dimensions instead feed the page's
  existing `hullShape()` unchanged — the same function already used for the
  boat itself — so hull rendering for N contacts is exactly as tested and
  correct as it is for one. This is a deliberate departure from the
  `review-issue` recommendation to reuse `footprint` directly; the tradeoff
  is one small per-contact conversion (mirroring
  `ais_contact_tracker.calculatePolygon`'s A/B/C/D unpacking, without its
  polygon-building step) against not duplicating rotation/translation logic
  client-side.
- **Popup content**: name-or-MMSI, callsign, ship/cargo type, speed, course,
  navigational status; renders correctly with only `mmsi`/position present
  (no type-5/24 seen yet).

## Files to Change

| File | Change |
|------|--------|
| `marine_web_view/marine_web_view/ais_renderer.py` | New node |
| `marine_web_view/launch/ais_renderer_launch.py` | New launch file |
| `marine_web_view/web/index.html` | AIS layer, toggle, popups, poll |
| `marine_web_view/package.xml` | `<depend>marine_ais_msgs</depend>` |
| `dependencies.repos` | Add `marine_ais` entry |
| `marine_web_view/README.md` | `ais_renderer` section + parameter table |
| `marine_web_view/test/test_launch_params.py` | Add `ais_renderer.py` pair to `PAIRS` |
| `marine_web_view/test/test_page_layers.py` | Add `live/ais.geojson` marker to `test_expected_layers_are_present` |
| `marine_web_view/test/test_ais_renderer.py` | New unit tests (GeoJSON shape, expiry, one-object-per-publish) |
| `marine_web_view/test/test_dry_run_needs_no_aws.py` | Extend the no-AWS-on-dry-run gate to `ais_renderer` |
| `marine_web_view/test/test_node_upload_wiring.py` | Extend upload-wiring checks to `ais_renderer` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Read-only display of already-public AIS data; no control surface. Popup makes provenance (position-only vs. static-info-seen) visible rather than guessing. |
| A change includes its consequences | `package.xml`, `dependencies.repos`, README, and the two hand-maintained test lists (`PAIRS`, layer markers) are all in the file list above, not left implicit. |
| Only what's needed | No change to sonar-coverage path, no boat-side change, no pandy bring-up launch — matches the issue's Non-goals. |
| Improve incrementally | Third renderer following the same two-renderer pattern; no new upload/parameter convention invented. |
| Test what breaks | GeoJSON shape, expiry, dry-run-constructs-no-client, one-object-per-publish, plus the two existing drift guards (`test_launch_params.py`, `test_page_layers.py`) extended rather than bypassed. |
| Capture decisions, not just implementations | Expiry/cadence/geometry/popup decisions recorded above with rationale, including the deliberate departure from the `review-issue` geometry recommendation. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| This repo's `docs/decisions/*` (0001–0013, sonar/bathymetry/nav transports) | No | None govern a read-only AIS display layer; the closest (ADR-0008, live sonar coverage transport) is a different transport this issue explicitly does not touch. |
| ROS 2 conventions (parameter naming, launch file shape, license header) | Yes (informal) | `ais_renderer.py`/`ais_renderer_launch.py` mirror `state_renderer`'s existing shape exactly, including the BSD-3 header block. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add a node parameter | `ais_renderer_launch.py` forwarding, README table, `test_launch_params.py` `PAIRS` | Yes |
| Add a page layer/source | `test_page_layers.py` marker tuple | Yes |
| Add a package-level runtime dep | `package.xml`, `dependencies.repos` (CI source-dep gap) | Yes |
| Add an upload path | `test_dry_run_needs_no_aws.py`, `test_node_upload_wiring.py` (both currently enumerate `state_renderer`/`coverage_renderer` explicitly) | Yes |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_web_view/README.md` gains
  an `ais_renderer` node section and a `web/index.html` AIS-layer paragraph;
  without it the parameter-documentation test (`test_launch_params.py`)
  fails, and the page's new toggle would otherwise be as undocumented as the
  gap #341 originally exposed.
- **Agent-instruction candidates**: None — this is entirely a same-pattern
  extension of an already-documented renderer convention
  (`bucket`/`key`/`profile`/`dry_run`/`local_path`/`interval`); nothing here
  is a new pitfall for `.agent/knowledge/`.

## Open Questions

- Should `ais_contact_tracker`'s in-memory contact ever be pruned from
  `ais_renderer`'s own tracking dict once expired, or only excluded from the
  published snapshot while retained indefinitely in memory? (Plan above
  prunes it — bounded memory, and a re-appearing MMSI after `contact_timeout`
  has no stale state to conflict with.) Flagging as it is a small design call
  not explicit in the issue.
- `contact_timeout` default (600 s) and `interval` default (10 s) are this
  plan's judgment calls, not values pinned by the issue or by
  `bizzyboat_project11/config/ais.yaml` — worth a sanity check against
  observed Piscataqua AIS traffic density during the pandy end-to-end
  verification pass before considering the defaults final.

## Estimated Scope

Single PR.
