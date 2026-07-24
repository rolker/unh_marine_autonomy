# Plan: marine_bathymetry_store: S-102 area importer (discover/fetch/convert/import)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/278

## Context

NOAA S-102 gridded bathymetry (HDF5, MLLW, depth + 1σ uncertainty) is published on
AWS with a machine-readable tile catalog (GeoPackage: per-tile URL, SHA256,
resolution, issuance). Verified coverage for both Aug-2026 ops areas. The store
wants ellipsoidal up-positive heights with NaN nodata (`GeoTiffImportOptions`
contract); its per-import affine cannot express the spatially-varying MLLW→
ellipsoid shift, so conversion is per-cell in a new converter, and the importer
receives already-store-convention GeoTIFFs. Chart layer doesn't exist until #275
— this tool targets an explicit `--layer` (`reference` for now).

## Approach

1. **Discover** — `src/s102/catalog.{hpp,cpp}`: newest catalog via S3
   ListObjectsV2 (`CPLHTTPFetch` + `CPLParseXMLString`) under `ed3.0.0/_CATALOG/`;
   open via `/vsicurl/` (or local path — the test seam); OGR bbox filter →
   `std::vector<TileRecord>{tile_id, url, sha256, resolution_m, issuance}`.
   Trust catalog URLs only, never path conventions (upstream "Deleware_Bay" typo).
2. **Fetch** — `src/s102/fetch.{hpp,cpp}`: download to `<cache>/tiles/`,
   SHA256-verify (OpenSSL EVP), record in `<cache>/registry.json`
   (nlohmann_json, temp+rename atomic) keyed `tile_id → {issuance, sha256,
   filename}`; skip when issuance+sha unchanged (idempotent re-run); delete+refetch
   on mismatch. `file://`/plain-path URLs supported for offline tests.
3. **Convert** — `src/s102/convert.{hpp,cpp}`: GDAL-open tile (S102 driver),
   depth band 1 / uncertainty band 2, remap 1e6→NaN, per-cell
   `height = mllw_z(lat,lon) − depth` through a `VerticalDatumProvider`
   interface (`std::optional<double> mllwHeight(lat, lon)`); cells where the
   provider returns nullopt become nodata (never silently unshifted). Write
   2-band Float64 GeoTIFF, source CRS/geotransform preserved.
   Providers: `ConstantOffsetProvider` (tests, explicit scratch use) +
   `marine_vertical_datum` adapter wired when #274 lands.
4. **CLI** — `src/s102_import_main.cpp`: `s102_import --area
   <minLon,minLat,maxLon,maxLat> --store <dir> --layer <survey|reference>
   --cache <dir> [--catalog <url|path>] [--offline] --datum <vdatum|constant:<m>>`.
   Level per tile via `gggs::Level::fromCellSize(resolution_m)`; import through
   existing `importGeoTiff` (default affine — input already ellipsoidal),
   `reference_writable` gating as in `import_geotiff_main.cpp`.
5. **Tests** (gtest, no network): catalog query on a fixture gpkg; fetch
   idempotency + corrupted-SHA rejection via `file://`; convert sign/nodata/
   non-positive-σ cases on synthetic rasters with `ConstantOffsetProvider`
   (S102 driver not required — converter accepts any 2-band GDAL raster);
   CLI end-to-end offline smoke into a temp store.
6. **Docs** — README section (workflow: fetch-before-deploy, offline re-run,
   datum convention); update `.agents/README.md` package-inventory line.

## Files to Change

| File | Change |
|------|--------|
| `marine_bathymetry_store/src/s102/catalog.{hpp,cpp}` | new — discover stage |
| `marine_bathymetry_store/src/s102/fetch.{hpp,cpp}` | new — fetch + cache registry |
| `marine_bathymetry_store/src/s102/convert.{hpp,cpp}` | new — per-cell datum + GeoTIFF out |
| `marine_bathymetry_store/src/s102_import_main.cpp` | new — CLI orchestrator |
| `marine_bathymetry_store/CMakeLists.txt` | new exe + OpenSSL dep + tests |
| `marine_bathymetry_store/package.xml` | `libssl-dev`/`openssl` rosdep dep |
| `marine_bathymetry_store/test/test_s102_*.cpp` + fixtures | new tests |
| `marine_bathymetry_store/README.md`, `.agents/README.md` | docs |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness / no silent failure | SHA256 verify hard-fails; nullopt datum → nodata not unshifted; atomic registry write; non-positive σ → NaN (never-reliable) matching importer semantics |
| Verify against source | Store conventions read from `geotiff_import.hpp`; level mapping from `gggs/level.h`; catalog fields verified against live bucket 2026-07-24 |
| CI independence | All tests offline (fixture gpkg, `file://`, synthetic rasters, mocked datum) |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| uma ADR-0002 §D2/§D4 | Yes | Multi-level import (level per tile resolution); datum converted once at import, per-cell (stricter than the affine §D4 anticipated) |
| uma ADR-0010 | Yes | Ellipsoidal invariant; chart-destination staged behind #275 (explicit `--layer`, `reference` interim); fetch/cache = boat-as-offline-tooling; finest-governs 4m/16m clipping deliberately left to #275 regeneration |
| uma ADR-0005 | Yes | Coarse store-level provenance only (post-#248) — no per-cell stamping |
| ws ADR-0009 | Yes | No pip deps; OpenSSL via rosdep |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| New importer CLI | package README + `.agents/README.md` inventory | Yes |
| S-102 corpus cache convention | ADR-0010 housekeeping (ecosystem doc reframe) | No — already tracked under ADR-0010 housekeeping |
| Update automation | s57_tools#28 cron composition | No — follow-on by design |

## Open Questions

- [ ] OpenSSL as the SHA256 dep (rosdep `libssl-dev`) — acceptable, or prefer a
      vendored sha256 to avoid the link dep?
- [ ] If #274 lands before this PR merges: wire the `marine_vertical_datum`
      provider in this PR, or keep it a fast-follow commit? (Interface seam is
      identical either way.)
- [ ] Default `--cache` location: leave required-explicit until the
      `~/data/stores` → `~/data/world` migration lands? (Plan assumes yes.)

## Estimated Scope

Single PR.
