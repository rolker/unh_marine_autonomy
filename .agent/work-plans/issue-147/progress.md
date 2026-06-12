---
issue: 147
---

# Issue #147 — Bathymetric store Phase 2: import (bag-replay, GeoMapSheet, GeoTIFF) with per-day epoch model

## Corpus Inventory
**Status**: complete
**When**: 2026-06-11 21:55 -0400
**By**: Claude Code Agent (Fable 5)

Surveyed `~/data/logs` (144 GB) for ROS-bag M3 multibeam coverage to drive the
Phase-2 bag-replay importer.

### M3 detection coverage (ROS bags)

| Day | Source | Sessions | M3 det msgs | Hours |
|-----|--------|----------|-------------|-------|
| 2026-06-09 | `gabby/logs/bizzy_m3/bag_2026-06-09T14.51.50_m3_detections` | 1 | 55,100 | 1.6 |
| 2026-06-10 | `gabby/logs/bizzyboat_sonar/2026-06-10T17-57-25+00-00` | 1 | 104,979 | 2.4 |
| 2026-06-11 | `gabby/logs/bizzyboat_sonar/2026-06-11T{16-30-24,19-29-20}+00-00` | 2 | 253,044 | 3.7 |

Total: **3 days / 4 sessions / 413k SonarDetections messages**. June 11's two
same-day sessions are a natural test case for the within-epoch cross-session
fusion rule (ADR-0002 A1.2); the three days exercise the multi-day epoch model.

### Replayability

- June-9 standalone bag is self-contained: `/bizzy/sensors/m3/detections`
  (SonarDetections) **and** `/bizzy/sensors/m3/soundings` (PointCloud2),
  `/bizzy/odom`, `/tf` (188k msgs), `/tf_static`, sound speed.
- June-10/11 sonar-recorder bags carry detections + `/bizzy/odom` + `/tf` +
  `/tf_static` (no pre-projected soundings topic).
- 98 of 101 sonar-recorder sessions (Apr 6 – Jun 8) contain **no** M3 topics —
  M3 recording in bags begins 2026-06-09. Earlier M3 data lives in QINSy
  projects on mercat (`mercat/QPS-Projects`, not ROS bags) and would enter the
  store as processed-layer products (BAG/GeoTIFF export), not via bag replay.

### Verification items — both PASS (2026-06-11)

- [x] **Earth-frame chain present in bags.** June-10 bag's `/tf` carries
  `earth -> bizzy/map` plus the full cube pose chain
  (`bizzy/map -> bizzy/base_link_north_up` level frame,
  `bizzy/odom -> bizzy/map_tide` tide frame) and `/tf_static` has the sensor
  mounts (`bizzy/base_link -> bizzy/m3`). A cold replay georeferences from the
  bag alone. (Boat config: `map_frame: bizzy/map`; georeferencing goes through
  the ECEF `earth` frame — same pattern `bag_to_geotiff.cpp:280,314` uses.)
- [x] **Pipeline inputs confirmed.** `detections_to_pointcloud` consumes
  `SonarDetections` + `Odometry` and produces PointCloud2 `soundings`;
  `cube_bathymetry_node` consumes `soundings` and transforms into `map_frame`.
  Sound speed is embedded per-ping (`ping_info.sound_speed` in
  `SonarDetections`) — no external sound-speed feed needed at replay. June-9
  bag also recorded the pre-projected `soundings` topic; June-10/11 bags need
  the detections→soundings stage re-run.

### Boat layout decision (Roland, 2026-06-12)

Store root is a **sibling of the log root, not nested in it** — bags are the
append-only raw record, the store is derived working knowledge (re-derivable
from bags, so no special backup discipline):

```
<data root>/logs/...                 # existing per-session bags, untouched
<data root>/bathymetry/store/        # persists across sessions & reboots
    draft/<date>/                    # today live-fused; past dates compacted
    processed/<date>/
```

Flow: underway, live importer (Phase 3) writes `draft/<today>/` incrementally;
evenings, dev replays the day's bags → compacted epoch → copied back (Phase-6
sync later). The concrete path + `store_dir` parameter land in `bizzyboat.yaml`
with the Phase-3 node — record it as a config item in that sub-issue.

### Distribution transport decision (Roland, 2026-06-12) — for the Phase-6 sub-issue

Per ADR-0002 §D6, robot and operator each hold a **full independent store**;
sync is manifest-diff (`{layer/epoch/GridIndex → content-hash}`), single-writer
per epoch (draft = boat-produced, processed = operator/dev-produced — no merge
case), and epochs are immutable after compaction so only today's live-fused
epoch is ever hot. Transport in two stages:

- **Interim / dockside: rsync (or plain cp).** The store is directories of
  immutable files — rsync is already a correct sync protocol for it, zero
  code. Compacted epochs ride the evening bag-offload, copy back the same way.
- **Phase 6 / underway: ROS-msg protocol over udp_bridge.** During ops
  udp_bridge is the managed link (rate-limited, prioritized); a node pair does
  manifest exchange + tile chunks as messages, idempotent by content-hash.
  "rsync semantics, udp_bridge transport." This is the bounded, change-only
  payload that durably fixes the unh_echoboats_project11#250 monolithic-grid
  downlink failure (`clear_grid` stays the interim until then).

Nothing in the tile format, layout, or epoch rules changes for Phase 6 — the
sync layer is additive, and camp#90's directory watcher works identically
whichever transport filled the operator store.

### Tooling gotchas found driving the corpus (2026-06-12)

- `detections_to_pointcloud` is a **lifecycle node**: replay harnesses must
  configure+activate it or it records nothing, silently (cost a 25-min no-op).
  Conversion script fixed; durable fix is the projection-refactor
  (rolker/cube_bathymetry#43) which removes the live-replay step entirely.
- Replay rate is capped (~5x) by the node's best-effort `SensorDataQoS` — no
  backpressure, silent drops. Same refactor removes the cap (direct bag
  reader, in-process, deterministic — required for content-hash-stable
  compaction per ADR-0002 §D6).

### First end-to-end run (2026-06-12, overnight)

Bags → per-day CUBE replay → `import_geotiff` → epoch store → change maps,
all real data:

- **Pier store** (`~/data/logs/analysis/2026-06-11_pier_grid/store`): three
  replayed draft epochs — 2026-06-09 (133,406 cells), -10 (274,573), -11
  (215,228; both sessions through ONE CUBE run = A1.2 compaction), 8 tiles,
  15 MB. Conversions ran ≥99.9% faithful at 5x replay.
- **Change maps**: 09→10 = 59k doubly-observed cells, median +0.08 m,
  95th |Δ| 0.39 m. 10→11 = 134k cells, median +0.14 m, 95th |Δ| 2.26 m
  (June-11 includes the sidescan/range-experiment sessions — noisier).
  Small positive medians look like day-to-day datum/tide residual — exactly
  the systematic signal the epoch model is meant to expose.
- **No cross-day combined CUBE run was made — deliberately**: the "all data
  combined" view is the store's newest-epoch composite (369k cells), per
  A1.1; fusing days in CUBE would violate the model.
- **Lake Massabesic store** (`~/data/bathymetry/massabesic/store`):
  chart/2026-06-12 prior from the CCOMJHC/ccomjhc_project11#55 contour
  GeoTIFF — 69,157,035 cells, 142 tiles, 242 MB, uncertainty 3.0 m const,
  `--depth-scale -1`. **OPEN: --depth-offset 0** — chart heights are
  lake-surface-relative until day-1 M3-vs-prior change-map calibration
  measures the surface's ellipsoidal height; then re-import (one command).
- Import guard added en route: non-positive uncertainty = missing (real
  June-9 product had 5,971 cells with uncertainty 0).

Renders: `pier_epochs_and_change.png`, `june09_pier_0.5m.png` (analysis dir),
`store_chart_preview.png` (massabesic dir).

### Key discovery

`cube_bathymetry` already ships **`bag_to_geotiff`** — an offline multi-bag
reader (merges bags by timestamp, builds a `GeoMapSheet` via the bag's
earth-frame TF, exports GeoTIFF). The boat config comment confirms the intent:
"Logging detections lets soundings + grid be re-derived offline by replaying
through the pipeline." The Phase-2 replay harness should extend or mirror this
tool (adding epoch-tagged store import per ADR-0002 A1) rather than starting
from scratch. Note layering: the store cannot depend on cube (ADR-0002), so the
harness lives cube-side or as a bridge package, importing into the store via
its public API.
