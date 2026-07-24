# Plan: marine_bathymetry_store: S-102 area importer (discover/fetch/convert/import)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/278

## Context

NOAA S-102 gridded bathymetry (HDF5, MLLW, depth + 1σ uncertainty) is published on
AWS with a machine-readable tile catalog (GeoPackage: per-tile URL, SHA256,
resolution, issuance; columns `TILE_ID/REGION/SUBREGION/ISSUANCE/S102V30/
S102V30_SHA256/Resolution/UTM`). Verified coverage for both Aug-2026 ops areas.
The store wants ellipsoidal up-positive heights with NaN nodata, **geographic
WGS84 only** (`geotiff_import.cpp:80-86` rejects projected rasters) — S-102
tiles are projected (UTM, e.g. EPSG:32619), so convert must warp. The importer's
per-import affine cannot express the spatially-varying MLLW→ellipsoid shift, so
conversion is per-cell in the converter; `importGeoTiff` receives
store-convention GeoTIFFs. Chart layer doesn't exist until #275 — this tool
targets an explicit `--layer` (`reference` for now). **Guardrail (ADR-0010 D7):
until #276 (confidence-gate cost model) lands, `s102_import` output is for
scratch/offline stores only — never a store a live costmap reads. Targeting
`reference` does not lift this; README must state it.**

## Approach

1. **Discover** — `src/s102/catalog.{hpp,cpp}`: newest catalog via S3
   ListObjectsV2 (`CPLHTTPFetch` + `CPLParseXMLString`) under `ed3.0.0/_CATALOG/`;
   open via `/vsicurl/` (or local path — the test seam); OGR bbox filter →
   `std::vector<TileRecord>{tile_id, url, sha256, resolution_m, issuance}`.
   `Resolution` is a string (`"4m"`/`"16m"`) — parse to metres (unit-tested).
   Trust catalog URLs only, never path conventions (upstream "Deleware_Bay" typo).
2. **Fetch** — `src/s102/fetch.{hpp,cpp}`: download to `<cache>/tiles/`,
   SHA256-verify (OpenSSL EVP), record in `<cache>/tiles.json` (named to avoid
   collision with the store's own `registry.json`; nlohmann_json, temp+rename
   atomic) keyed `tile_id → {issuance, sha256, filename}`; skip when
   issuance+sha unchanged; delete+refetch on mismatch. `file://`/plain-path
   URLs supported for offline tests.
3. **Convert** — `src/s102/convert.{hpp,cpp}`: GDAL-open tile with pinned
   options `DEPTH_OR_ELEVATION=DEPTH`, `NORTH_UP=YES` (no silent sign/row
   flips on GDAL upgrades); **hard-fail unless `VERTICAL_DATUM_ABBREV=MLLW`**
   (Great Lakes LWD etc. must not be shifted as MLLW); bands located by
   `Description` (`depth`/`uncertainty`), nodata from `GetNoDataValue()`
   (fallback 1e6) → NaN **before** warping; **warp to geographic WGS84**
   (nearest-neighbour — value-preserving, keeps each depth/σ pair coherent) at
   resolution matching the tile; then per-cell
   `height = mllw_z(lat,lon) − depth` through `VerticalDatumProvider`
   (`std::optional<double> mllwHeight(lat, lon)`); nullopt → nodata (never
   silently unshifted). Write 2-band Float64 GeoTIFF to `<cache>/converted/`
   (retained — inspectable, cheap, re-runs skip). Providers:
   `ConstantOffsetProvider` (tests, scratch use) + `MarineVerticalDatumProvider`
   adapter over #274 (`src/s102/vdatum_provider.{hpp,cpp}`).
4. **Import** — CLI `s102_import` (`src/s102_import_main.cpp`): `--area
   <minLon,minLat,maxLon,maxLat> --store <dir> --layer <survey|reference>
   --cache <dir> --datum <vdatum|constant:<m>> [--catalog <url|path>]
   [--offline] [--cell-size <m>] [--force]`. `--cell-size` mirrors
   `import_geotiff` (store default level; default 0.5). Per-tile import level =
   `gggs::Level::fromCellSize(parsed resolution)`; `reference_writable` gating
   as in `import_geotiff_main.cpp`. Store-level provenance: StoreMetadata
   source entry "NOAA S-102 ed3.0.0" + catalog issuance (same coarse ADR-0005
   mechanism `import_geotiff` uses). **Whole-pipeline idempotency**: store-side
   sidecar `<store>/s102_imported.json` records `tile_id → issuance` imported;
   unchanged tiles skip convert+import (per-store, so one cache can feed many
   stores); `--force` overrides. Matches the issue's "re-run is a no-op"
   acceptance.
5. **Tests** (gtest, no network): catalog query + resolution-string→level
   mapping on a fixture gpkg; fetch idempotency + corrupted-SHA rejection via
   `file://`; convert: projected-input warp, sign (`depth 5, mllw_z −28 →
   height −33`), nodata, non-positive σ, non-MLLW hard-fail — on synthetic
   projected rasters with `ConstantOffsetProvider` (S102 driver not required);
   CLI end-to-end offline smoke into a temp store incl. no-op re-run.
6. **Docs** — README section (fetch-before-deploy workflow, offline re-run,
   datum convention, **the #276 scratch-stores-only guardrail**); update
   `.agents/README.md` package-inventory line **and "Known build requirements"**
   (new inter-package dep on `marine_vertical_datum`).

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/src/s102/catalog.{hpp,cpp}` | new — discover stage |
| `marine_bathymetry_store/src/s102/fetch.{hpp,cpp}` | new — fetch + `tiles.json` cache registry |
| `marine_bathymetry_store/src/s102/convert.{hpp,cpp}` | new — warp + per-cell datum + GeoTIFF out |
| `marine_bathymetry_store/src/s102/vdatum_provider.{hpp,cpp}` | new — adapter over marine_vertical_datum (#274) |
| `marine_bathymetry_store/src/s102_import_main.cpp` | new — CLI orchestrator + store-side import sidecar |
| `marine_bathymetry_store/CMakeLists.txt` | new internal static lib `s102_import_core` (headers under `src/s102/`, not installed; OpenSSL + marine_vertical_datum PRIVATE) linked by CLI + tests; keeps deps out of the exported store lib |
| `marine_bathymetry_store/package.xml` | `libssl-dev` rosdep dep; `<depend>marine_vertical_datum</depend>` |
| `marine_bathymetry_store/test/test_s102_*.cpp` + fixtures | new tests (catalog, fetch, convert, CLI smoke) |
| `marine_bathymetry_store/README.md` | s102_import docs + #276 guardrail |
| `.agents/README.md` | inventory line + Known build requirements (marine_vertical_datum) |

**Merge dependency**: this PR does not build until #274 lands in jazzy
(adapter + package dep). Wire-in-PR was Roland's explicit call 2026-07-24.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness / no silent failure | SHA256 verify hard-fails; non-MLLW datum hard-fails; nullopt datum → nodata not unshifted; nodata NaN'd before warp (no smearing); atomic registry writes; non-positive σ → NaN (never-reliable) matching importer semantics |
| Verify against source | Store conventions + projected-input rejection read from source; catalog schema, S102 driver behaviour, SHA256 match verified live 2026-07-24 (plan review) |
| CI independence | All tests offline (fixture gpkg, `file://`, synthetic projected rasters, mocked datum) |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| uma ADR-0002 §D2/§D4 | Yes | Multi-level import (level per tile resolution); datum converted once at import, per-cell |
| uma ADR-0010 | Yes | Ellipsoidal invariant; chart destination staged behind #275 (`reference` interim); **#276 precondition = explicit scratch-stores-only guardrail in plan + README**; fetch/cache = boat-as-offline-tooling; finest-governs 4m/16m clipping deliberately left to #275 regeneration |
| uma ADR-0005 | Yes | Coarse store-level provenance via StoreMetadata (source + issuance), no per-cell stamping |
| ws ADR-0009 | Yes | No pip deps; OpenSSL via rosdep |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| New importer CLI | package README + `.agents/README.md` inventory | Yes |
| New dep on marine_vertical_datum | `.agents/README.md` Known build requirements; merge order after #274 | Yes |
| S-102 corpus cache convention | ADR-0010 housekeeping (ecosystem doc reframe) | No — already tracked under ADR-0010 housekeeping |
| Update automation | s57_tools#28 cron composition | No — follow-on by design |

## Open Questions

None — the original three resolved by Roland 2026-07-24 (rosdep OpenSSL;
wire `marine_vertical_datum` provider in this PR; `--cache` required-explicit).
Plan-review findings (2026-07-24, 3 must-fix + 9 suggestions) folded in above;
the optional PR-split suggestion was declined — #274 is in progress and
in-PR wiring was an explicit decision.

## Estimated Scope

Single PR (large but coherent; merge-gated on #274).
