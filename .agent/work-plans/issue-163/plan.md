# Plan: Bathy store — add Chart source layer (load contour prior; lake constant-offset datum)

## Issue
https://github.com/rolker/unh_marine_autonomy/issues/163

## Context
Phase 1 (#141) shipped `SourceLayer{Processed, Draft}`; `Chart` is reserved but
unimplemented (`bathy_cell.hpp` comment, absent from `source_layers_by_priority`,
no `chart/` case in `tile_io`). It was gated on the mru_transform vertical-datum
work (ADR-0002 §D4). For **Lake Massabesic that gate is a constant offset** (the
lake-surface ellipsoidal height, 52.3 m), not the ocean VDatum path — so Chart
lands now. The on-disk prior (`~/data/bathymetry/massabesic/store/chart/2026-06-12/`,
142 tiles) is **raw depth-below-surface** (NH GRANIT 5-ft contours, negative-down);
the store's `BathyCell.depth` is **ellipsoidal height**, so the prior must be
converted *at import* (§D7) — that conversion is A2, not a raw load.

## Approach (one issue, two PRs)

### PR A1 — core: `SourceLayer::Chart` (datum-agnostic, `unh_marine_autonomy`)
1. **`bathy_cell.hpp`** — add `Chart = 2`; append to `source_layers_by_priority`
   (size 2→3; `source_layer_count`/`BathymetryStore::layers_` adapt automatically).
   Chart is last → lowest query priority (best-source falls through to it). Update
   the "reserved" doc comment.
2. **`tile_io.cpp`** — add `case SourceLayer::Chart: return "chart";` in
   `layerDirName`. `save()`/`load()` already iterate `source_layers_by_priority`,
   so `chart/` round-trips with no further change; WGS84-validated load applies
   as-is.
3. **Read-only enforcement** — make Chart **read-only by default**: `set(Chart,…)`
   throws unless the store was explicitly opened chart-writable (the A2 importer's
   mode). This makes the prior unclobberable by live Draft/CUBE ingest everywhere
   except the dedicated importer. (See Open Question 1 — mechanism choice.)
4. **gtests** — `test_query`: best-source fall-through to Chart; `test_tile_io`:
   chart tile round-trip + an existing processed/draft store still loads (chart/
   absent → skipped); `test_store`: `set(Chart)` rejected unless chart-writable.

### PR A2 — importer: contour prior → Chart layer (`cube_bathymetry`, sensors_ws)
5. **Chart import path** (separate worktree/PR on cube_bathymetry): read the raw
   contour tiles (depth-below-surface), add the per-tile datum offset from
   `resolve_datum(lat, lon, entries, /*vdatum=*/nullopt)`, write ellipsoidal Chart
   tiles via the store opened chart-writable. cube_bathymetry (sensors_ws) is above
   `platforms_ws`, so it can link `mru_transform`'s `datum_config` and the store —
   keeping the core datum-agnostic.
6. **Datum entry** — add the `massabesic` polygon `DatumEntry` (`chart_datum_z:
   52.3`) to the datum config `chart_datum_node` loads (single source of truth).
7. **Verify** against the on-disk prior: a −13.716 m (45 ft) cell imports to 38.58 m
   ellipsoidal; 142 tiles covered; re-import idempotent (content-hash unchanged).

## Files to Change
| File | Change |
|------|--------|
| `marine_bathymetry_store/include/.../bathy_cell.hpp` | add `Chart`, extend priority array, doc |
| `marine_bathymetry_store/src/tile_io.cpp` | `layerDirName` chart case |
| `marine_bathymetry_store/include/.../bathymetry_store.hpp` (+ src) | chart read-only mode + guarded `set` |
| `marine_bathymetry_store/test/{test_query,test_tile_io,test_store}.cpp` | Chart coverage |
| *(A2, cube_bathymetry repo)* | chart importer + `massabesic` datum entry |

## Principles Self-Check
| Principle | Consideration |
|---|---|
| Reuse over duplication | Reuses `tile_io`, `query` overlay, `resolve_datum()`; no new tile format or datum math. |
| Layering | Core stays datum-agnostic (core_ws); conversion in the sensors_ws importer that may link platforms_ws. |
| Do it right / completeness | Read-only guard + tests, not just the enum; idempotent import. |

## ADR Compliance
| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 §D3 (source-layer overlay) | Yes | Chart added as lowest-priority non-destructive layer. |
| ADR-0002 §D5 (per-tile GeoTIFF) | Yes | Chart reuses the same 3-band Float64 tile_io. |
| ADR-0002 §D7 (convert at import) | Yes | Datum offset applied in A2 importer; store stores ellipsoidal. |
| ADR-0002 §D4 (datum gate) | Partially | Lake = constant offset, decoupled from ocean VDatum (deferred). |

## Consequences
| If we change... | Also update... | Included? |
|---|---|---|
| `source_layer_count` 2→3 | any code assuming exactly 2 layers | Verified: array-sized; query/tile_io iterate the array — none hardcode 2 |
| Add Chart to save/load scan | existing processed/draft stores | Back-compatible (missing `chart/` skipped) — covered by a test |
| Store now stores a converted prior | sim harness (#76), costmap layer (#164) consume ellipsoidal Chart | Downstream issues already assume ellipsoidal |

## Open Questions
1. **Read-only mechanism — RESOLVED (implemented in A1).** Chart is read-only by
   default: `set(Chart,…)` throws `std::logic_error` unless the store is
   constructed `chart_writable=true` (importer only). `load()`/`getOrCreateTile`
   stay unguarded, so the runtime loads the prior into a read-only store. The
   alternative (runtime `setLayerReadOnly()` flag, default writable) was rejected —
   default-read-only is the stronger guarantee.
2. **A2 worktree** is a separate cube_bathymetry repo PR — confirm we stack it after
   A1 lands (A1 has no cube dependency; A2 depends on A1's chart-writable API).

## Estimated Scope
Two PRs across two repos. A1 (core) is small and self-contained; A2 (importer)
depends on A1's chart-writable API + the existing #148 import tooling patterns.
