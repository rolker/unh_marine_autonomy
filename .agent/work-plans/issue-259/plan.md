# Plan: Survey Indexer + Query CLI

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/259

## Context

Stage 1 of the #258 survey-data exploration umbrella. The goal is a fast "given a
location, which bags and time ranges saw it?" answer across a whole survey — in
seconds, without replaying bags. The deliverable is a regenerable SQLite sidecar
(`survey_index.db`) next to the stores, and a CLI to query it.

The bounded single-pass TF pattern is already proven in two places in this repo:
- `marine_sidescan_mosaic/src/sidescan_mosaic_bag.cpp` — Garmin GCV sidescan importer
- `cube_bathymetry/src/import_bag_main.cpp` — M3 detections → CUBE bathy store

The indexer reuses that pattern exactly rather than inventing a new TF walk.

Known topics from the existing offline tools:
- MBES soundings: `/bizzy/sensors/m3/detections` (`marine_acoustic_msgs/SonarDetections`)
- Sidescan port: `/bizzy/sensors/sidescan/garmin_sidescan/sonar_image_port`
- Sidescan starboard: `/bizzy/sensors/sidescan/garmin_sidescan/sonar_image_starboard`
- Sidescan nadir (range): `/bizzy/sensors/sidescan/garmin_sidescan/nadir_depth`

## Approach

### Package home decision

New package **`marine_survey_index`** inside `unh_marine_autonomy` (core_ws). Reasons:
- GGGS (`marine_autonomy`) and all stores (`marine_tiled_raster_store`, `marine_bathymetry_store`, etc.) already live in core_ws/unh_marine_autonomy.
- `marine_tools` (sensors_ws) is a sonar-to-pointcloud bridge with no GGGS dependency; adding GGGS as a dep would be a layering violation.
- The indexer is naturally a companion to the existing store packages, not a sensor driver.

### Implementation steps

1. **Create `marine_survey_index` package** — `CMakeLists.txt`, `package.xml` (MIT license, ADR-0008 headers), with deps: `rosbag2_cpp`, `tf2`, `marine_autonomy` (GGGS), `marine_acoustic_msgs`, `sensor_msgs`, `tf2_msgs`. Link SQLite3 via `target_link_libraries(... sqlite3)`. No ROS 2 nodes — this is a pair of offline CLI binaries.

2. **Implement `survey_index_bag` binary** (`src/survey_index_bag_main.cpp`):
   - Opens (or creates) `survey_index.db` with schema below.
   - Scans a directory for bags (recursive, `*.db3` or MCAP) OR accepts explicit bag paths.
   - Skips bags already in the `bags` ledger with matching `size_bytes + mtime_ns` (incremental re-run safety).
   - For each new bag: single interleaved chronological pass (same pattern as `sidescan_mosaic_bag.cpp`):
     - Subscribes to `/tf`, `/tf_static` + the three sensor topic sets.
     - `tf2::BufferCore` with 60 s cache window, 3 s guard interval, same `kMaxPending` cap.
     - **MBES**: for each `SonarDetections` message, resolve the `earth→frame_id` transform, compute the bounding lat/lon box of the beam footprint (across-track extent), enumerate all GGGS tiles at `--level` that overlap the box, record as a ping in the accumulator.
     - **Sidescan port/stbd**: for each `RawSonarImage`, resolve the transducer `earth` pose, compute the port or starboard ground-range extent footprint (same as `sidescan_mosaic_bag.cpp`), enumerate tiles.
   - Accumulator: per `(tile, sensor_type, topic)` open interval. Consecutive pings within `--merge-gap` (default 5.0 s) of each other extend the interval; a wider gap closes the interval and opens a new one.
   - After the pass, flush all open intervals to the `passes` table.
   - Insert the bag into the `bags` ledger.

3. **Implement `survey_index_query` binary** (`src/survey_index_query_main.cpp`):
   - Args: `--point <lat> <lon>` or `--box <lat_min> <lon_min> <lat_max> <lon_max>`, optional `--radius <m>`, `--level <N>`, `--sensor <type>`, `--db <path>`, `--json`.
   - Converts the query geometry to a set of GGGS tile indices at the requested level.
   - Joins `passes` with `bags` on those tiles, filtered by sensor if requested.
   - Groups results by bag path, prints intervals sorted by `t_start`.
   - Human-readable default; `--json` emits a JSON array for downstream tools.

4. **Schema** (`src/schema.h` — shared between the two binaries):

```sql
CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);
-- version=1; checked at open; mismatch → error with regen hint

CREATE TABLE IF NOT EXISTS bags (
  id         INTEGER PRIMARY KEY,
  path       TEXT    NOT NULL UNIQUE,
  size_bytes INTEGER NOT NULL,
  mtime_ns   INTEGER NOT NULL,
  indexed_at_ns INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS passes (
  id          INTEGER PRIMARY KEY,
  bag_id      INTEGER NOT NULL REFERENCES bags(id),
  level       INTEGER NOT NULL,
  tile_row    INTEGER NOT NULL,
  tile_col    INTEGER NOT NULL,
  sensor_type TEXT    NOT NULL,  -- extends ADR-0005 D3 sensor_class vocabulary: 'mbes-bathy' (D3),
                                 -- plus channel-split 'sidescan-port'/'sidescan-stbd' (D3 'sidescan' + channel;
                                 -- query CLI treats --sensor sidescan as matching both)
  topic       TEXT    NOT NULL,
  t_start_ns  INTEGER NOT NULL,
  t_end_ns    INTEGER NOT NULL,
  ping_count  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS passes_tile ON passes(level, tile_row, tile_col);
CREATE INDEX IF NOT EXISTS passes_bag  ON passes(bag_id);
```

5. **Tests** (`test/`) — all bag-I/O-free (plan-review must-fix: cover the core
   correctness paths, not just plumbing):
   - `test_schema.cpp`: opens a fresh DB, verifies tables and version row, checks that re-open doesn't error.
   - `test_interval_merge.cpp`: unit test for the interval-accumulator logic (gap below threshold → merged; gap above → split); no bag I/O needed.
   - `test_tile_enumeration.cpp`: footprint→GGGS-tile enumeration — a known lat/lon box at a known level yields exactly the expected tile set (single-tile, tile-boundary-straddling, and multi-tile swath cases).
   - `test_query_join.cpp`: builds an in-memory index with known passes, runs the query join (point and box, with and without `--sensor` filter incl. `sidescan` matching both channels), asserts the returned bag/interval set.

6. **Update `docs/sonar_ecosystem.md`**: add a "Survey indexer/query" row in the Arc 1 Reprocess section referencing #258/#259.

## Files to Change

| File | Change |
|------|--------|
| `marine_survey_index/CMakeLists.txt` | new — two executables + tests; links sqlite3 |
| `marine_survey_index/package.xml` | new — declares deps |
| `marine_survey_index/src/survey_index_bag_main.cpp` | new — indexer binary |
| `marine_survey_index/src/survey_index_query_main.cpp` | new — query binary |
| `marine_survey_index/src/schema.h` | new — shared schema init + version check |
| `marine_survey_index/test/test_schema.cpp` | new — schema unit test |
| `marine_survey_index/test/test_interval_merge.cpp` | new — accumulator unit test |
| `marine_survey_index/test/test_tile_enumeration.cpp` | new — footprint→tile enumeration test (plan-review must-fix) |
| `marine_survey_index/test/test_query_join.cpp` | new — query tile-join test (plan-review must-fix) |
| `docs/survey_index_schema.md` | new — durable schema contract doc for #258 stages 2–5 (plan-review suggestion) |
| `docs/sonar_ecosystem.md` | update — add survey indexer row |
| `.agents/README.md` | update — add `marine_survey_index` to Package Inventory (plan-review suggestion) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Human-readable default output; `--json` for tool integration; regenerable sidecar — bags are always the data of record |
| Capture decisions, not just implementations | Package home, schema, TF pattern, and `sensor_type` vocabulary recorded here; schema version check enforces stability |
| A change includes its consequences | Tests included; ecosystem doc updated; schema documented in this plan for stages 2–5 of #258 |
| Only what's needed | Two binaries; no GUI, no store writes, no viewer integration (all deferred to later stages) |
| Improve incrementally | Stage 1 of 5; delivers usable CLI immediately |
| Test what breaks | `test_interval_merge` covers the core merge logic that is easy to get wrong at edge cases; `test_schema` covers the DB-open contract that stages 2–5 depend on |

## ADR Compliance

*(WS) = workspace ADR (`ros2_agent_workspace/docs/decisions/`); (P) = project ADR
(`unh_marine_autonomy/docs/decisions/`). Numbers overlap between the two sets —
labeled per plan-review suggestion.*

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 (WS — Worktree isolation) | Yes | Working in feature/issue-259 worktree throughout |
| ADR-0005 (P — Provenance registry) | Yes | `sensor_type` column *extends* ADR-0005 D3 `sensor_class` vocabulary: `mbes-bathy` verbatim; `sidescan-port`/`sidescan-stbd` = D3 `sidescan` + channel suffix (query maps `sidescan` → both). `platform`/`sensor` at bag granularity via `bags` table metadata extension is a follow-up |
| ADR-0008 (WS — ROS 2 conventions) | Yes | `package.xml` with MIT license, ROS 2 naming, standard CMakeLists patterns; no nodes so no node-naming concern |
| ADR-0009 (WS — Python package management) | No | No Python components |
| ADR-0013 (WS — progress.md vocabulary) | Yes | Entries use canonical vocabulary |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| SQLite schema | Stage 2–5 consumers (viewer pane, interval loader) must be aware of schema_version | Yes — version check at open enforces this |
| `sensor_type` vocabulary | Any future stage that filters by sensor type | Yes — ADR-0005 D3 vocabulary used; additive to add new values |
| Topic defaults | `survey_index_bag` CLI defaults | Yes — configurable via `--mbes-topic`, `--port-topic`, `--stbd-topic` flags |
| `docs/sonar_ecosystem.md` | Nothing else | Yes — included in Files to Change |

## Decisions (Roland, 2026-07-13 plan checkpoint)

- **GGGS level default = L14 (~54 m tiles), both sensors** *(revised 2026-07-13,
  implementation round — supersedes the "store-native level" checkpoint answer,
  which had conflated grid size with cell size: the bathy store's L10 tiles have
  ~0.9 m cells but are ~870 m across — far too coarse as a selection unit)*.
  L14 is a target-inspection neighbourhood (target + shadow + context), and a
  finer index is strictly more information: L14 keys roll up to the stores'
  native tiles (bathy L10, sidescan L13) through the GGGS parent hierarchy, so
  stage 2 can still join against store tiling by aggregation. Row cost remains
  trivial (a swath touches a handful of 54 m tiles across-track).
  `--mbes-level` / `--sidescan-level` / `--level` overrides remain.
- **Sidescan sensor_type split**: `sidescan-port` / `sidescan-stbd` in the DB for
  channel fidelity; the query CLI treats `--sensor sidescan` as matching both.
  This *extends* the ADR-0005 D3 `sensor_class` vocabulary (`sidescan` + channel
  suffix); it does not redefine it.
- **Merge gap default 5.0 s**, exposed as `--merge-gap <s>`; calibrate against
  Massabesic data (a turn splitting intervals is acceptable and informative).

## Estimated Scope

Single PR. Two new binaries + two tests + one doc update — fits in one review cycle.
