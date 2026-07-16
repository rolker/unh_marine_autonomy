# Plan: survey_index: decimated nav track table for the explorer map (schema bump)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/265

## Context

The survey index (`survey_index.db`, schema v1) records where each sonar looked
via the `passes` table. The explorer map (issue #258, stages 3–5) also needs a
lightweight nav track — a decimated sequence of vehicle positions per bag — to
render coverage overviews without replaying full bags.

The `survey_index_bag` indexer already resolves `earth`→sensor TF transforms for
every ping. It can accumulate nav positions from the same TF buffer at negligible
extra cost. The new `nav_track` table stores those positions pre-decimated; the
map pane queries them by bag id or bounding box.

This is a schema v1 → v2 bump. There are no migrations: the index is a regenerable
sidecar; a v1 DB opened by a v2 tool fails with the existing "re-run the indexer"
error already tested in `test_schema.cpp`.

**Decimation strategy decision (was TBD in issue body):** **distance-based**, one
point per ≥ N meters (default 10 m, configurable via `--nav-stride-m`). Rationale:
visual density on the map should be uniform in space, not time; a stationary ship
produces no extra points; the math is a single Haversine call per candidate point.
Time-based would bunch up during station-keeping and under-sample high-speed runs.
Hybrid adds complexity without a clear gain.

**Accessor contract for #258 stages 3/5:**
- `queryNavTrack(db, bag_id) → vector<NavPoint>` — full ordered track for one bag
- `queryNavTrackInBox(db, lat_min, lon_min, lat_max, lon_max) → vector<NavPoint>` — all points inside a geographic bounding box (for map overview)

## Approach

1. **Bump schema version and add DDL** — increment `kSchemaVersion` to 2 in
   `schema.hpp`; add `nav_track` DDL in `schema.cpp` (see table below). The
   `openIndexDb` version gate already handles the mismatch error path.

2. **Add `NavPoint` struct and accessor declarations** — add to `query.hpp`:
   `NavPoint {bag_id, t_ns, latitude, longitude}`, `queryNavTrack(db, bag_id)`,
   `queryNavTrackInBox(db, lat_min, lon_min, lat_max, lon_max)`.

3. **Implement accessors** — add to `query.cpp`: two simple `SELECT` queries with
   `ORDER BY t_ns`. The box query uses `latitude BETWEEN ? AND ?` and `longitude
   BETWEEN ? AND ?` (the spatial index on `nav_track` makes this fast enough;
   the explorer map queries one viewport at a time).

4. **Collect nav positions in the indexer** — in `survey_index_bag_main.cpp`,
   after each successful `flush_front()` call that yields a valid earth pose,
   compare the new lat/lon to `last_accepted_pos`; if the Haversine distance is
   ≥ `--nav-stride-m` (default 10 m) OR no position has been accepted yet, push
   `{t_ns, lat, lon}` into a per-bag `nav_points` vector. One helper function
   `haversineMeters(lat1, lon1, lat2, lon2) → double` (degrees in, metres out).

5. **Persist nav points** — in the same per-bag `BEGIN … COMMIT` block, after
   flushing passes, INSERT all `nav_points` into `nav_track` bound to the bag id.

6. **Tests** — three test targets, no bag I/O:
   - `test_schema.cpp`: update table-count assertion (3 → 4), add `nav_track`
     column-count check.
   - `test_nav_decimation.cpp`: unit-test `haversineMeters` (known pairs), and
     the distance-gate logic (points < stride → skipped; ≥ stride → kept; first
     point always kept).
   - `test_query_join.cpp`: add nav_track accessor tests — insert known `nav_track`
     rows into an in-memory DB, assert `queryNavTrack(bag_id)` and
     `queryNavTrackInBox(box)` return the right subsets.

7. **Update `docs/survey_index_schema.md`** — add schema v2 section with the
   `nav_track` DDL, column semantics, decimation note, and accessor contract.

## Files to Change

| File | Change |
|------|--------|
| `include/marine_survey_index/schema.hpp` | Bump `kSchemaVersion` to 2; add `NavPoint` struct |
| `src/schema.cpp` | Add `nav_track` DDL to `kSchemaDdl`; add spatial index |
| `include/marine_survey_index/query.hpp` | Declare `queryNavTrack`, `queryNavTrackInBox` |
| `src/query.cpp` | Implement both nav_track accessor functions |
| `src/survey_index_bag_main.cpp` | Add `haversineMeters`, per-bag nav collection and INSERT |
| `test/test_schema.cpp` | Update table-count assert 3→4; add nav_track check |
| `test/test_nav_decimation.cpp` | New: test `haversineMeters` and distance-gate logic |
| `test/test_query_join.cpp` | Add nav_track accessor tests |
| `CMakeLists.txt` | Register `test_nav_decimation` target |
| `docs/survey_index_schema.md` | Document schema v2: `nav_track` table and accessor contract |

## Schema Addition (v2)

```sql
CREATE TABLE IF NOT EXISTS nav_track (
  id         INTEGER PRIMARY KEY,
  bag_id     INTEGER NOT NULL REFERENCES bags(id) ON DELETE CASCADE,
  t_ns       INTEGER NOT NULL,   -- UNIX nanoseconds (earth-frame TF stamp)
  latitude   REAL    NOT NULL,   -- WGS-84 degrees
  longitude  REAL    NOT NULL    -- WGS-84 degrees
);
CREATE INDEX IF NOT EXISTS nav_track_bag  ON nav_track(bag_id, t_ns);
CREATE INDEX IF NOT EXISTS nav_track_geo  ON nav_track(latitude, longitude);
```

`NavPoint` struct mirrors these columns plus `bag_id` for the box-query path.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Modularity and Decoupling | Nav accessors added to `query.hpp/cpp`; indexer logic is self-contained. No new package deps. |
| Iterative, Validated Evolution | Schema v2 is additive (new table only); v1 DBs fail loud, not silently corrupt. |
| Standards Compliance | REP-105 (earth frame), WGS-84 lat/lon. Distance calc uses Haversine (standard). |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0005 (provenance registry / sensor_class) | No — nav track is not a sensor class, it is a vehicle position record. | N/A |
| ADR-0008 (license / copyright headers) | Yes — new source files need MIT + UNH-CCOM header | All new `.cpp/.hpp` files include the standard header |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `kSchemaVersion` 1→2 | All existing `survey_index.db` files must be regenerated | Yes — documented in schema.md; the error message already says "re-run the indexer" |
| `nav_track` table DDL | `docs/survey_index_schema.md` | Yes — step 7 |
| `queryNavTrack` / `queryNavTrackInBox` API | `marine_perception_tools` (#258 stage 3/5) consumers | Noted — API is stable in this PR; #258 builds against it |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.
  (Decimation strategy pinned to distance-based 10 m default; accessor API defined above.)

## Estimated Scope

Single PR.
