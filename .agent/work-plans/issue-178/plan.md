# Plan: marine_bathymetry_store tile-format migration — time→Int64 tile + per-cell source-index band + registry (amend ADR-0002 D5; ADR-0005 D2/D8)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/178

## Context

The bathy store currently persists each tile as a 3-band `Float64` GeoTIFF
(depth, uncertainty, timestamp) — all `Float64` because a single GeoTIFF has one
band dtype and the timestamp was forced to match.  Two problems: (1) `Float64`
Unix seconds is not ROS-native (`rclcpp::Time` is int64 ns) and gives only ~0.4 µs
resolution rather than nanosecond-exact; (2) ADR-0005 D2/D8 (merged #179)
requires a per-cell local source-index band so multi-platform fusion can track
which sensor contributed each cell.

ADR-0005 explicitly parks the bathy adoption on this issue (#178). Both changes
land on the same files (`bathy_cell.hpp`, `bathymetry_tile.hpp`, `tile_io.hpp`)
so they ship as one migration.

Prerequisites confirmed:
- ADR-0005 (#179) and ADR-0006 (#180) are MERGED.
- Phase-1 store (#141) is MERGED to `jazzy`.
- No field data committed to the durable layer (pre-production; no persisted tiles
  in the store directory to worry about, but implementor should verify before the
  first commit by checking the store directory on disk).

## Source-Index Encoding Decision

**Chosen: parallel `uint16_t` tile (separate file, separate band dtype).**

Rationale:

- A source index is logically and type-distinct from depth/uncertainty (float) and
  time (int64). Co-locating it in a `Float64` tile would require exact-in-float
  representation (valid for uint16, but fragile under future registry growth) and
  would force a 4-band `Float64` tile even for the hot-path costmap reader that
  never needs the source dimension.
- The Part 1 time-tile separation establishes the principle: separate bands of
  different dtype → separate tile file. Source index (uint16) follows the same
  rule consistently.
- `uint16_t` (`GDT_UInt16`) is already an explicit instantiation in
  `marine_tiled_raster_store/tile_io.cpp`; no new GDAL mapping is needed for the
  source tile itself.
- Aligns with ADR-0006 (sidescan backscatter store) which uses the same uint16
  source band encoding — a generic "read source band" path works identically
  across both stores.

## Approach

### Part 1 — Time band → separate `Int64` ns tile

1. **`bathy_cell.hpp`** — Change `BathyCell::timestamp` from `double` (Unix
   seconds) to `int64_t` (nanoseconds since epoch, ROS-native). Update the field
   comment. Remove the `double` justification comment that no longer applies.

2. **`bathymetry_tile.hpp`** — Split `BathymetryTile` into two underlying rasters:
   - `value_raster_` (`TiledRasterTile<double>`, 2 bands: depth + uncertainty)
   - `time_raster_` (`TiledRasterTile<int64_t>`, 1 band: timestamp ns)
   Update `set()`, `get()`, `dirty()`/`clearDirty()`/`markDirty()` to operate on
   both. Add typed band accessors `timestampBand()` returning `std::vector<int64_t>&`.
   The public `using Raster = TiledRasterTile<double>` stays for the value raster;
   add `using TimeRaster = TiledRasterTile<int64_t>`.

3. **`marine_tiled_raster_store/tile_io.cpp`** — Add `int64_t` explicit
   instantiation (all four templates: `saveTile`, `loadTile`, `saveTiles`,
   `loadTiles`) and the corresponding `gdalType<int64_t>()` specialization
   returning `GDT_Int64`. GDAL 3.8.4 is installed locally; this is the load-bearing
   GDAL version constraint for this migration.

4. **`marine_bathymetry_store/tile_io.hpp`** — Update the file-level doc comment:
   remove "3-band `Float64`" / "`Float64` is deliberate" language; replace with
   "2-band `Float64` (depth, uncertainty) + 1-band `Int64` (timestamp ns) +
   1-band `UInt16` (source index)". Update `saveTile` / `loadTile` signatures to
   communicate two separate files (see step 5).

5. **`marine_bathymetry_store/tile_io.cpp`** — Rewrite persistence to write/read
   two files per tile per layer:
   - `<level>_<row>_<col>.tif` — 2-band `Float64` (depth, uncertainty); NaN
     no-data on both bands.
   - `<level>_<row>_<col>_time.tif` — 1-band `Int64` (timestamp ns); no no-data
     tag (0 = unset).
   Update `save()` / `load()` to write/read both files. On load: if the time file
   is missing (pre-migration tile), treat timestamp as 0 for all cells (fallback
   path, logged as a warning). Update `kBathyBandCount` → remove and replace with
   `kValueBandCount = 2` and `kTimeBandCount = 1`.

### Part 2 — Per-cell source-index band + `registry.json` sidecar

6. **`bathy_cell.hpp`** — Add `uint16_t source_index = 0` to `BathyCell` (0 =
   no-data/unset, per ADR-0005 D4: source_id 0 is the no-data sentinel). Update
   `hasData()` to remain depth-only (source_index is not a data-presence signal).

7. **`bathymetry_tile.hpp`** — Add a third underlying raster:
   - `source_raster_` (`TiledRasterTile<uint16_t>`, 1 band: source index)
   Update `set()` / `get()` to read/write `source_index`. Add `sourceBand()`
   accessors returning `std::vector<uint16_t>&`.

8. **`marine_bathymetry_store/tile_io.cpp`** — Add a third file per tile per layer:
   - `<level>_<row>_<col>_source.tif` — 1-band `UInt16` (source index); no-data
     value = 0.
   On load: if `_source.tif` is missing, fill source_index = 0 for all cells
   (compatible with single-platform Phase-1 data mapping to "default entry").
   Update `save()` / `load()` accordingly.

9. **New file: `marine_bathymetry_store/include/marine_bathymetry_store/registry.hpp`** —
   Define `SourceRegistry` (a thin wrapper over `nlohmann::json` or `std::map`) with:
   - `struct SourceRecord { std::string source_id; std::string platform;
     std::string sensor; std::string sensor_class; std::string campaign;
     std::string datum; };`  (ADR-0005 D3 core schema + bathy extension `datum`)
   - `uint16_t registerSource(const SourceRecord &)` — intern a record, return its
     local index; idempotent on `source_id`.
   - `std::optional<SourceRecord> lookup(uint16_t index) const`
   - `void saveRegistry(const std::string & store_root_dir)` — write-then-rename
     atomic: write `registry.json.tmp`, then `std::filesystem::rename()` to
     `registry.json`. No partial-write corruption.
   - `void loadRegistry(const std::string & store_root_dir)` — load from
     `registry.json` if present; no-op if absent (fresh store).

10. **`marine_bathymetry_store/src/registry.cpp`** — Implement `SourceRegistry`.
    Use `nlohmann/json.hpp` (already a ROS 2 jazzy dependency). Index 0 is reserved
    as the no-data/unset sentinel and must never be assigned to a real record.

11. **`marine_bathymetry_store/tile_io.hpp`** and **`tile_io.cpp`** — Add
    `saveRegistry()` and `loadRegistry()` thin wrappers that call
    `SourceRegistry::saveRegistry/loadRegistry` so callers of the tile_io API
    can trigger registry persistence without touching SourceRegistry directly.
    Registry is store-wide (not per-layer), so `save()` calls `saveRegistry()` once
    at the end; `load()` calls `loadRegistry()` once at the start.

### Part 3 — ADR-0002 documentation amendment

12. **`docs/decisions/0002-bathymetric-data-store.md`** —
    - Add an amendment header entry after the existing `#151` note:
      `**Amended 2026-06-20 ([#178](…)):** D5 revised — time band moved to a
      separate Int64 ns tile; per-cell source-index band (UInt16) added alongside
      depth+uncertainty; registry sidecar added. See ADR-0005 D2/D8 and ADR-0006
      for the cross-store alignment.`
    - D5 body: update "3 bands … Float64" → "2-band Float64 (depth, uncertainty) +
      1-band Int64 (timestamp ns) + 1-band UInt16 (source index)"; remove "source
      layer is not a band" rationale sentence (it was correct for single-platform;
      replaced by the per-cell source-index design with local-index + registry);
      add cross-reference to ADR-0005 D2/D8 and #178.
    - D6 content-hash section: confirm/add that the content hash covers all three
      tile files (value + time + source) for a complete re-arbitration signal.

### Tests

13. **`test_tile_io.cpp`** — Update all tests for the new 3-file layout:
    - `RoundTripPreservesCells`: use `int64_t` ns timestamp values (not
      `1.78e9` float seconds); verify exact round-trip.
    - `ChartRoundTripsAndLoadsIntoReadOnlyStore`: update timestamp to int64_t ns.
    - All tests: fix any `EXPECT_DOUBLE_EQ(draft->timestamp, ...)` → use
      integer-exact `EXPECT_EQ`.
    - Add `RoundTripPreservesSourceIndex`: populate `source_index` in two cells,
      save, load, verify round-trip.
    - Add `MissingTimeTileLoadsAsZero`: save a value tile, delete the time tile,
      load, verify timestamp == 0 for all cells (fallback for pre-migration data).
    - Add `MissingSourceTileLoadsAsZero`: same for source_index.
    - Add `RegistryAtomicWrite`: call `saveRegistry`, verify `registry.json` exists
      and `registry.json.tmp` does not remain.

14. **`test_store.cpp`** — Update timestamp values from float seconds to int64_t ns:
    `1000.0` → `1'000'000'000'000LL` (1 s in ns), `1.0` → `1'000'000'000LL`, etc.
    Add `SetGetRoundTripWithSourceIndex`: set a cell with `source_index = 3`, get
    it back, verify.

15. **`test_query.cpp`** (regression — shallowest-reliable carve-out, ADR-0005 D5) —
    Add `ShallowestReliableUnaffectedBySourceIndex`: populate a tile with cells
    from two different source indices; verify `shallowestReliable()` returns the
    shallowest reliable depth regardless of `source_index` value. This directly
    demonstrates the D5 safety carve-out: the navigation-safety query ignores the
    registry priority axis. (The existing `ShallowestReliable*` tests continue to
    verify the uncertainty gate and NaN behaviour.)

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/include/marine_bathymetry_store/bathy_cell.hpp` | `timestamp` → `int64_t` ns; add `source_index uint16_t`; update comments |
| `marine_bathymetry_store/include/marine_bathymetry_store/bathymetry_tile.hpp` | Split into value raster (2-band Float64) + time raster (1-band Int64) + source raster (1-band UInt16) |
| `marine_bathymetry_store/include/marine_bathymetry_store/tile_io.hpp` | Update doc comment; add registry save/load declarations |
| `marine_bathymetry_store/src/tile_io.cpp` | Rewrite to 3-file per tile; add registry save/load delegation; add missing-file fallback paths |
| `marine_bathymetry_store/include/marine_bathymetry_store/registry.hpp` | New: `SourceRegistry` with `SourceRecord`, intern, lookup, atomic save/load |
| `marine_bathymetry_store/src/registry.cpp` | New: implement `SourceRegistry` (nlohmann/json, atomic write-then-rename) |
| `marine_tiled_raster_store/src/tile_io.cpp` | Add `int64_t` explicit instantiations + `gdalType<int64_t>()` → `GDT_Int64` |
| `marine_bathymetry_store/test/test_tile_io.cpp` | Update all tests for 3-file layout; add round-trip + fallback + registry tests |
| `marine_bathymetry_store/test/test_store.cpp` | Update timestamp values to int64_t ns; add source_index round-trip test |
| `marine_bathymetry_store/test/test_query.cpp` | Add `ShallowestReliableUnaffectedBySourceIndex` regression test |
| `docs/decisions/0002-bathymetric-data-store.md` | Part 3: amendment header; D5 body update; D6 content-hash note |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | `shallowestReliable()` is untouched in logic; a new regression test explicitly confirms the D5 safety carve-out holds with multi-source tiles. |
| Modularity and Decoupling | `int64_t` instantiation goes in `marine_tiled_raster_store` (generic layer); bathy-specific semantics stay in `marine_bathymetry_store`. Registry is its own header/source, not entangled with tile I/O beyond the save/load wrappers. |
| Standards Compliance | `int64_t` ns is ROS-native; `GDT_Int64` is GDAL 3.5+ (3.8.4 installed); `nlohmann/json` is a standard ROS 2 jazzy dep. |
| Only what's needed | No speculative features; registry schema is exactly the ADR-0005 D3 core + bathy `datum` extension. |
| A change includes its consequences | Tests updated in this PR; downstream #164 (costmap — depth is still band-1, likely unaffected but must be verified post-merge) and #175 (CAMP layer band-layout check) flagged for follow-through. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 D5 | Directly amended | Part 3 updates D5 text; amendment pointer added. |
| ADR-0005 D2/D8 | Yes — this is the bathy adopter | Part 2 implements the per-cell source-index band + registry sidecar exactly as D2/D8 specifies; source-index encoding decision (parallel uint16 tile) recorded here per D2's "left to #178". |
| ADR-0005 D5 (safety carve-out) | Yes | `shallowestReliable()` unchanged in logic; regression test added to prove it. |
| ADR-0002 D6 (content hash) | Watch | D6 specifies content hash covers the tile payload. With 3 files per tile, the hash must cover all three; confirmed/added in the D6 doc amendment. No Phase-1 hash implementation exists yet, so this is a doc note, not a code change. |
| ADR-0001 (adopt ADRs) | Yes | Amendment follows the cross-reference addendum pattern from #151. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `BathyCell::timestamp` dtype | All callers that write/read `double` seconds | Yes — test_store, test_tile_io timestamps converted to int64_t ns |
| `BathymetryTile` now holds 3 rasters | `save()` / `load()` writes/reads 3 files | Yes |
| New `_time.tif` / `_source.tif` files | Old pre-migration single-file tiles: load falls back gracefully | Yes — missing-file fallback with 0-fill |
| `DepthSample::timestamp` in `query.hpp` | Still `double` for consumer API? | Yes — change to `int64_t` to be consistent; `forEachCellBestSource` callers get the correct type |
| `registry.json` | Written atomically (write-then-rename) | Yes |
| Post-merge: #164 costmap reader | Verify depth is still band-1 of value tile | No — follow-up (#164), depth band unchanged so likely unaffected |
| Post-merge: #175 CAMP layer | Band-layout check post-merge | No — follow-up (#175) |

## Open Questions

- [ ] No open questions — plan is review-plan-ready. (Encoding decision is settled
  above; dependency status is confirmed; no field data to migrate.)

## Estimated Scope

Single PR. All changes touch the same package pair
(`marine_bathymetry_store`, `marine_tiled_raster_store`) and the same
`docs/decisions/0002-*.md` file. No cross-repo changes required.
