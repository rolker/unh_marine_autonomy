---
issue: 278
---

# Issue #278 — marine_bathymetry_store: S-102 area importer (discover/fetch/convert/import)

## Plan Authored
**Status**: complete
**When**: 2026-07-24 15:35 -0400
**By**: Claude Code Agent (Claude Fable 5)

**Plan**: `.agent/work-plans/issue-278/plan.md` at `54011a0`
**Branch**: feature/issue-278 at `54011a0`
**Phases**: single

### Open questions
- [x] OpenSSL (rosdep `libssl-dev`) as the SHA256 dep, vs vendored sha256 — **rosdep OpenSSL** (Roland 2026-07-24)
- [x] Wire marine_vertical_datum provider in this PR if #274 lands in time, else fast-follow — **wire in this PR** (Roland 2026-07-24; PR now depends on #274)
- [x] `--cache` stays required-explicit until ~/data/stores→~/data/world migration lands — **confirmed** (Roland 2026-07-24)

## Plan Review
**Status**: complete
**When**: 2026-07-24 16:48 -0400
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-278/plan.md` at `9323f48`
**PR**: PR-less (`--issue 278`, branch `feature/issue-278`)
**Verdict**: changes-requested

Live verification done during this review (GDAL 3.8.4, NOAA `noaa-s102-pds`
`ed3.0.0`, catalog `Navigation_Tile_Scheme_20260724_132709.gpkg`, tile
`102US005BOSDB262267.h5`): the S102 driver reads ed3.0.0 fine (bands
`depth`/`uncertainty`, Float32, NoData 1e6, `VERTICAL_DATUM_ABBREV=MLLW`),
SHA256 matches the catalog, catalog columns are
`TILE_ID/REGION/SUBREGION/ISSUANCE/S102V30/S102V30_SHA256/Resolution/UTM`.
Two of the must-fixes below come out of that check.

### Findings
- [x] (must-fix) Convert stage cannot preserve source CRS — S-102 tiles are **projected** (sample tile is EPSG:32619 UTM 19N; the catalog even carries a `UTM` column), while `importGeoTiff` throws `not a geographic WGS84 raster` (`src/geotiff_import.cpp:80-86`). Convert must warp to geographic WGS84 (and set nodata before resampling so 1e6/NaN doesn't smear); per-cell lat/lon for the datum shift also comes free after the warp — `plan.md:35`
- [x] (must-fix) In-PR wiring of the `marine_vertical_datum` provider (#274, still OPEN) has no entry in Files to Change: `package.xml` `<depend>marine_vertical_datum</depend>`, `find_package` + link in `CMakeLists.txt`, and the adapter source file are all unlisted. Also record the build-order consequence (`.agents/README.md` "Known build requirements") and that the PR cannot build/merge before #274 lands — `plan.md:37, 52-63`
- [x] (must-fix) No guardrail for ADR-0010 D7's hard precondition: the confidence-gate cost model (#276, OPEN) must land with or before the first chart-derived cells reach a store the costmap reads. Importing S-102 into `reference` sidesteps the `chart` layer name but not the costmap exposure. Plan should state (and README should document) that until #276 lands, `s102_import` targets scratch stores only, never a live boat store — `plan.md:16, 78`
- [x] (suggestion) Catalog field details differ from the plan's shorthand: `Resolution` is a **string** (`"4m"`, `"16m"`) needing a parse before `Level::fromCellSize`, and the URL/SHA columns are `S102V30`/`S102V30_SHA256`. Add a unit test for the resolution→level mapping — `plan.md:22-24, 41`
- [x] (suggestion) Don't hardcode the 1e6 nodata remap — the driver reports it per band (`GetNoDataValue`); read it and fall back to 1e6. Same for band identity: prefer the band `Description` (`depth`/`uncertainty`) over fixed indices — `plan.md:32`
- [x] (suggestion) Pin the S102 open options explicitly (`DEPTH_OR_ELEVATION=DEPTH`, `NORTH_UP=YES`) rather than relying on driver defaults, so a GDAL upgrade can't silently flip sign or row order under the `mllw_z − depth` formula — `plan.md:30-33`
- [x] (suggestion) Assert the tile's `VERTICAL_DATUM_ABBREV` is `MLLW` (it is exposed in the dataset metadata) and hard-fail otherwise — non-MLLW products (e.g. Great Lakes LWD) would otherwise be shifted with the wrong reference, silently — `plan.md:33`
- [x] (suggestion) CMake/layout unspecified: whether `src/s102/*` joins the `marine_bathymetry_store` library or a separate target, and how the gtests include headers that live under `src/` (this package's convention is `include/marine_bathymetry_store/`). Pick one and say so; keep OpenSSL a PRIVATE link dep so it stays out of `ament_export_dependencies` — `plan.md:56-60`
- [x] (suggestion) `<cache>/registry.json` collides in name with the store's own `registry.json` (StoreMetadata/source registry). Rename the tile cache sidecar (e.g. `tiles.json`); also say where converted GeoTIFFs land (`<cache>/converted/` vs temp) and whether they are retained — `plan.md:26-27, 34`
- [x] (suggestion) Issue acceptance says "re-run is a no-op when issuances unchanged", but the plan's idempotency stops at fetch — convert + import would re-run every time. Either skip convert/import for unchanged tiles (registry-driven) or state explicitly that re-import is merely harmless, not skipped — `plan.md:27`
- [x] (suggestion) CLI omits the store's default cell size/level (`import_geotiff` takes `--cell-size`, default 0.5) and any `StoreMetadata` provenance (ADR-0005 coarse, store-level: source = NOAA S-102 ed3.0.0 + issuance). Decide both — with mixed 4m/16m tiles the default level choice and the single store-level metadata slot both need an answer — `plan.md:38-43`
- [x] (suggestion) Scope is on the large side for one PR (4 new modules + CLI + 4 test groups + docs) and stage 3 is blocked on #274. A split at the natural seam — discover+fetch (unblocked) then convert+import (needs #274) — would unblock half the work now; noted as an option, not a requirement, since the in-PR wiring was Roland's call — `plan.md:101-103`
- [ ] (note) `review-issue` was not run on #278 (no comments on the issue, no `## Issue Review` entry) — not penalized, just recorded.

## Findings Addressed
**Status**: complete
**When**: 2026-07-24 17:05 -0400
**By**: Claude Code Agent (Claude Fable 5)

All 3 must-fix + 8 suggestions folded into `plan.md` (one commit with this entry):
warp-to-geographic in convert (+ nodata-before-warp, projected-input test);
marine_vertical_datum wiring in Files to Change + package.xml + build-order/merge-gate
notes; #276 scratch-stores-only guardrail in Context/ADR table/README scope; catalog
schema corrections (string Resolution parse + S102V30 columns, unit test); nodata via
GetNoDataValue + bands by Description; pinned S102 open options; MLLW assert hard-fail;
CMake layout settled (internal static lib `s102_import_core`, PRIVATE deps); cache
sidecar renamed `tiles.json` + `converted/` retention stated; whole-pipeline idempotency
via store-side `s102_imported.json` + `--force`; `--cell-size` + StoreMetadata
provenance decided. **Declined**: PR split (suggestion 12) — #274 in progress, in-PR
wiring was Roland's explicit call; rationale recorded in plan. (note) review-issue
gap acknowledged, not retrofitted.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 21:54 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-278 at `d1da2a4`
**Mode**: pre-push
**Depth**: Deep (reason: ~2.5k new lines, network fetch + GDAL warp/subprocess + cross-layer vdatum wiring + costmap-feeding safety surface)
**Must-fix**: 1 | **Suggestions**: 4
**Round**: 1 | **Ship**: continue — one must-fix (S3 pagination), a cheap mechanical fix; batch with suggestions in one address-findings pass, then re-review

Specialists: static analysis (cpplint clean; cppcheck 3 sub-threshold nits dropped);
Claude Adversarial 2 passes (Lens A logic, Lens B systemic/safety); Local Adversarial
skipped (no Ollama server). Both adversarial passes also produced 5 plausible-but-wrong
findings — all refuted in the report (size_t-cast "overflow" ×2, GDALWarp double-close,
XML billion-laughs, TLS-not-verified) and not carried here.

### Findings
- [x] (must-fix) `findNewestCatalog` ignores S3 `ListObjectsV2` pagination (no `IsTruncated`/`NextContinuationToken` loop) — silently returns a stale/old catalog if >1000 objects exist under the prefix; violates the "newest catalog" contract. Cross-confirmed (lead + Lens A) — `src/s102/catalog.cpp:59`
- [x] (suggestion) Cache basename from `fs::path(record.url).filename()` doesn't strip `?query` or validate the tail (dir-escape already prevented); key on `tile_id` or sanitize — `src/s102/fetch.cpp:146`
- [x] (suggestion) Provenance `StoreMetadata.survey` becomes generic `"catalog.gpkg"` on discovered runs (catalog repointed to the cached copy before the filename read); record the real discovered-catalog name — `src/s102/run.cpp:190`
- [x] (suggestion) Comment the discovery trust boundary (catalog listing is TLS-trusted, not digest-verified, unlike tile payloads) — `src/s102/catalog.cpp:64`
- [x] (suggestion) Sweep orphaned `<cache>/tiles/*.part` files at cache open (accumulate on mid-download kill) — `src/s102/fetch.cpp:177`
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail is doc-only, not code-enforced — accepted at plan-review as a documented precondition; keep visible until #276 lands (deferred: tracking item, not a code change for this PR — code enforcement lands with #276; kept visible in the Implementation deferred list below)

## Implementation
**Status**: complete
**When**: 2026-07-24 22:05 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-278 at `9db4776`
**Addressed**: `## Local Review (Pre-Push)` (2026-07-24 21:54 +0000, branch `feature/issue-278` at `d1da2a4`)
**Commits**: `fd92f22`, `16a9974`, `611f0fc`, `a769323`, `9db4776`

Built `marine_bathymetry_store` clean and ran its suite after the fixes:
286 tests, 0 failures, 38 skipped. cpplint/format ran per-commit via
pre-commit (no `--no-verify`).

### Actions
- [x] (must-fix) S3 `ListObjectsV2` pagination in `findNewestCatalog` — now loops on `IsTruncated`/`NextContinuationToken` (URL-encoded token) so a `_CATALOG/` prefix with >1000 objects can't hide the newest catalog — `src/s102/catalog.cpp` (`fd92f22`)
- [x] (suggestion) Cache basename strips any `?query`/`#fragment` tail before landing on disk (filename() already drops dir components) — `src/s102/fetch.cpp` (`16a9974`)
- [x] (suggestion) Provenance `StoreMetadata.survey` records the real discovered-catalog filename (captured before the cached-alias repoint), falling back to the resolved path only for explicit/offline runs — `src/s102/run.cpp` (`611f0fc`)
- [x] (suggestion) Comment added at the catalog listing documenting the discovery trust boundary (TLS-trusted, not digest-verified; integrity begins at the per-tile SHA256) — `src/s102/catalog.cpp` (`a769323`)
- [x] (suggestion) `TileCache` sweeps orphaned `<cache>/tiles/*.part` temporaries at open (best-effort; only verified payloads are ever renamed into place) — `src/s102/fetch.cpp` (`9db4776`)
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail (deferred: tracking item — not a code change for this PR; code enforcement is #276's scope, kept visible in the source review entry until it lands)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-27 19:04 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-278 at `4a7faf5`
**Mode**: pre-push
**Depth**: Deep (reason: ~2.6k new lines, network fetch + GDAL warp/subprocess +
cross-layer vdatum wiring + costmap-feeding safety surface)
**Must-fix**: 1 | **Suggestions**: 8
**Round**: 2 | **Ship**: recommended — the one must-fix is a mechanical ctor
guard (verified against the marine_vertical_datum API contract) and the eight
suggestions are minor/robustness; address in one pass, then a light confirm
suffices. No design questions remain and must-fix is not rising (Round 1 also
had 1, since fixed).

Specialists: static analysis (ament_cpplint clean, "No problems found" across
all 15 changed C++ files); Claude Adversarial 2 passes (Lens A logic/edge cases,
Lens B systemic/safety); Local Adversarial skipped (no Ollama server on :11434).
Round-1's must-fix (S3 pagination) is confirmed correctly fixed, as are the four
Round-1 suggestions (basename query-strip, real discovered-catalog provenance,
trust-boundary comment, `.part` sweep). The `.part` sweep fix itself introduces
a minor concurrency suggestion (#8 below). Both adversarial passes also produced
several plausible-but-refuted findings — path-traversal via catalog filename,
EVP/CPLHTTPResult/CPLXMLNode/VSILFILE leaks, GDALWarp double-free, XML
billion-laughs (CPL minixml is non-validating), OpenSSL/vdatum leaking into the
package export — all traced and refuted, not carried here.

### Findings
- [ ] (must-fix) `MarineVerticalDatumProvider` ctor stores `make_vdatum_query()`
  without the `if (query)` check the API contract mandates — on grid-setup
  failure (missing/typo'd `--geoid`/`--vdatum-grids`) the factory returns an
  empty `std::function`, so `mllwHeight` returns nullopt for every cell and the
  whole import silently becomes all-nodata while exiting 0. Hard-fail in the ctor
  (fail-loud; no-silent-failure) — `src/s102/vdatum_provider.cpp:35`
- [ ] (suggestion) Warp `-srcnodata` is computed from the depth band only and
  broadcast to both bands; assumes `depth.nodata == uncertainty.nodata` (true for
  S-102's 1e6, but the code reads each band's nodata at :118 only to warn, then
  discards uncertainty's). Assert equal or pass per-band — `src/s102/convert.cpp:130`
- [ ] (suggestion) Cache on-disk path is keyed by URL basename while the registry
  is keyed by `tile_id`; two tile_ids sharing a basename collide on one file
  (self-healing via the on-access SHA check, but forces needless refetch). Key the
  local path by `tile_id` — `src/s102/fetch.cpp:160`
- [ ] (suggestion) `mllwHeight` returns `resolve_datum(...).chart_datum_z`, which
  a non-MLLW `--datum-config` polygon override could make non-MLLW — shifting an
  MLLW tile past the `VERTICAL_DATUM_ABBREV=MLLW` gate the converter enforces.
  Narrow (operator config) but the gate and provider check different datums;
  add a guard or doc note — `src/s102/vdatum_provider.cpp:47`
- [ ] (suggestion) `--area` parsing accepts NaN/inf (a NaN slips through the
  `min<max` guard into `SetSpatialFilterRect`) and ignores trailing garbage after
  `max_lat`. Reject non-finite and require the stream be exhausted —
  `src/s102_import_main.cpp:127`
- [ ] (suggestion) `--cell-size std::stod(...)` sits outside the `main` try block
  (unlike the wrapped `constant:` datum parse), so `--cell-size abc` throws
  uncaught → `std::terminate`/abort instead of a clean usage error. Wrap it —
  `src/s102_import_main.cpp:112`
- [ ] (suggestion) Output GeoTIFF flush occurs in the `DatasetPtr` destructor,
  which cannot report failure; a disk-full during the deferred DEFLATE flush
  yields a truncated tif that `importGeoTiff` reopens. Add an explicit
  flush + error check before returning — `src/s102/convert.cpp:207`
- [ ] (suggestion) The `.part` sweep in `TileCache`'s ctor unconditionally removes
  every `*.part`; two concurrent runs sharing one `--cache` (run.hpp advertises
  one cache feeding many stores) would delete each other's in-flight download.
  Make `.part` names unique per-process, or document the cache as single-writer —
  `src/s102/fetch.cpp:141`
- [ ] (suggestion) `run.hpp`'s "a partial run leaves the store consistent" claim
  holds only if the throw precedes `save()`; `save()` writes tiles incrementally
  with no rollback. Minor doc correction — `src/s102/run.hpp:83`
- [ ] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail remains
  doc-only, not code-enforced (`reference_writable` gates the layer, not
  costmap-liveness) — accepted at plan-review as a documented precondition; code
  enforcement lands with #276. Carried, not a new code change for this PR.

### Next step
Lifecycle: **Local Review** → (verdict changes-requested) → **address-findings**
→ re-review. One must-fix + eight suggestions are open above; the diff is not
pushed until a pre-push review returns **approved**. Hand off to a fresh-context
sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill address-findings
