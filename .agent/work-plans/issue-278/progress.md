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
- [x] (must-fix) `MarineVerticalDatumProvider` ctor stores `make_vdatum_query()`
  without the `if (query)` check the API contract mandates — on grid-setup
  failure (missing/typo'd `--geoid`/`--vdatum-grids`) the factory returns an
  empty `std::function`, so `mllwHeight` returns nullopt for every cell and the
  whole import silently becomes all-nodata while exiting 0. Hard-fail in the ctor
  (fail-loud; no-silent-failure) — `src/s102/vdatum_provider.cpp:35`
- [x] (suggestion) Warp `-srcnodata` is computed from the depth band only and
  broadcast to both bands; assumes `depth.nodata == uncertainty.nodata` (true for
  S-102's 1e6, but the code reads each band's nodata at :118 only to warn, then
  discards uncertainty's). Assert equal or pass per-band — `src/s102/convert.cpp:130`
- [x] (suggestion) Cache on-disk path is keyed by URL basename while the registry
  is keyed by `tile_id`; two tile_ids sharing a basename collide on one file
  (self-healing via the on-access SHA check, but forces needless refetch). Key the
  local path by `tile_id` — `src/s102/fetch.cpp:160`
- [x] (suggestion) `mllwHeight` returns `resolve_datum(...).chart_datum_z`, which
  a non-MLLW `--datum-config` polygon override could make non-MLLW — shifting an
  MLLW tile past the `VERTICAL_DATUM_ABBREV=MLLW` gate the converter enforces.
  Narrow (operator config) but the gate and provider check different datums;
  add a guard or doc note — `src/s102/vdatum_provider.cpp:47`
- [x] (suggestion) `--area` parsing accepts NaN/inf (a NaN slips through the
  `min<max` guard into `SetSpatialFilterRect`) and ignores trailing garbage after
  `max_lat`. Reject non-finite and require the stream be exhausted —
  `src/s102_import_main.cpp:127`
- [x] (suggestion) `--cell-size std::stod(...)` sits outside the `main` try block
  (unlike the wrapped `constant:` datum parse), so `--cell-size abc` throws
  uncaught → `std::terminate`/abort instead of a clean usage error. Wrap it —
  `src/s102_import_main.cpp:112`
- [x] (suggestion) Output GeoTIFF flush occurs in the `DatasetPtr` destructor,
  which cannot report failure; a disk-full during the deferred DEFLATE flush
  yields a truncated tif that `importGeoTiff` reopens. Add an explicit
  flush + error check before returning — `src/s102/convert.cpp:207`
- [x] (suggestion) The `.part` sweep in `TileCache`'s ctor unconditionally removes
  every `*.part`; two concurrent runs sharing one `--cache` (run.hpp advertises
  one cache feeding many stores) would delete each other's in-flight download.
  Make `.part` names unique per-process, or document the cache as single-writer —
  `src/s102/fetch.cpp:141`
- [x] (suggestion) `run.hpp`'s "a partial run leaves the store consistent" claim
  holds only if the throw precedes `save()`; `save()` writes tiles incrementally
  with no rollback. Minor doc correction — `src/s102/run.hpp:83`
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail remains
  doc-only, not code-enforced (`reference_writable` gates the layer, not
  costmap-liveness) — accepted at plan-review as a documented precondition; code
  enforcement lands with #276. Carried, not a new code change for this PR.
  (deferred: tracking item — code enforcement is #276's scope; the scratch-only
  precondition stays documented in run.hpp / s102_import_main.cpp until it lands.)

### Next step
Lifecycle: **Local Review** → (verdict changes-requested) → **address-findings**
→ re-review. One must-fix + eight suggestions are open above; the diff is not
pushed until a pre-push review returns **approved**. Hand off to a fresh-context
sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill address-findings

## Implementation
**Status**: complete
**When**: 2026-07-27 19:24 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-278 at `dcded4e`
**Addressed**: `## Local Review (Pre-Push)` (2026-07-27 19:04 +0000, branch `feature/issue-278` at `4a7faf5`) — Round 2
**Commits**: `f5b4ad0`, `79b0bbe`, `2761f78`, `dc2244c`, `c09f068`, `f8b33e1`, `e649684`, `dcded4e`

Built `marine_bathymetry_store` clean and re-ran its suite after the fixes:
286 tests, 0 failures, 38 skipped (unchanged baseline). ament_cpplint and
ament_uncrustify both "No problems found" across all six changed C++ files.
No pre-commit hook is installed in this worktree, so linters were run manually;
no `--no-verify` was used (nothing to bypass).

### Actions
- [x] (must-fix) `MarineVerticalDatumProvider` ctor now hard-fails when
  `make_vdatum_query()` returns an empty `std::function` (grid-setup failure) —
  no more silent all-nodata import exiting 0. Header `@throws` updated —
  `src/s102/vdatum_provider.cpp:38` (`f5b4ad0`)
- [x] (suggestion) Warp `-srcnodata` now built per source band (each band's own
  `GetNoDataValue`, fallback 1e6) instead of broadcasting the depth band's value
  — drops the `depth.nodata == uncertainty.nodata` assumption —
  `src/s102/convert.cpp` (`79b0bbe`)
- [x] (suggestion) Output GeoTIFF is now `FlushCache(false)`-flushed with an
  error check before return, so a disk-full during the deferred DEFLATE write is
  caught (fail-loud) rather than leaving a truncated tif for `importGeoTiff` —
  `src/s102/convert.cpp:207` (`2761f78`)
- [x] (suggestion) Cache on-disk file now keyed by a sanitized `tile_id`
  (+ URL extension), not the URL basename, so two tile_ids sharing a basename
  no longer collide; registry `filename` records the real on-disk name —
  `src/s102/fetch.cpp:160` (`dc2244c`)
- [x] (suggestion) Documented the tile cache as single-writer (one run per
  `--cache` at a time) at the `.part` sweep and on `S102ImportOptions.cache_dir`,
  since the sweep removes every `*.part` unconditionally —
  `src/s102/fetch.cpp:141`, `src/s102/run.hpp` (`c09f068`)
- [x] (suggestion) Corrected `run.hpp`'s partial-run consistency claim: `save()`
  is not transactional (incremental, no rollback), so a failure *during* save can
  leave the store partially written; only failures *before* save leave it
  untouched — `src/s102/run.hpp:83` (`f8b33e1`)
- [x] (suggestion) Documented that `--datum-config` polygon entries are trusted
  to be MLLW — `DatumEntry` carries no datum label, and the converter's MLLW gate
  validates the source tile, not operator polygons (no feasible code guard
  without a datum label on the entry) — `src/s102/vdatum_provider.hpp` (`e649684`)
- [x] (suggestion) Hardened CLI parsing: `--area` now rejects non-finite
  (NaN/inf) values and trailing garbage after `max_lat`; `--cell-size std::stod`
  wrapped in try/catch so bad input is a clean usage error, not `std::terminate`
  — `src/s102_import_main.cpp:112,127` (`dcded4e`)
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail (deferred:
  tracking item — code enforcement is #276's scope; the scratch-only precondition
  stays documented in run.hpp / s102_import_main.cpp until #276 lands)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off
to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-27 19:34 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-278 at `d887822` (code state `dcded4e`; HEAD is progress-only)
**Mode**: pre-push
**Depth**: Deep (reason: ~2.6k new lines, network fetch + GDAL warp/subprocess +
cross-layer vdatum wiring + costmap-feeding safety surface)
**Must-fix**: 1 | **Suggestions**: 1
**Round**: 3 | **Ship**: recommended — the single must-fix is a mechanical
try/catch wrap (verified against the sibling `constant:`/`--cell-size` paths in
the same file) and the one suggestion is narrow robustness. Must-fix count flat
across rounds (1→1→1), no design questions remain; address in one pass, then a
light confirm suffices.

Specialists: static analysis (ament_cpplint + ament_uncrustify "No problems
found" across all 15 changed C++ files; ament_cppcheck 2.13.0 self-skipped as a
known-slow version, not run); Claude Adversarial 2 passes (Lens A logic/edge,
Lens B systemic/safety); Local Adversarial skipped (no Ollama server on :11434);
Copilot off (default). Round-2's must-fix (vdatum ctor empty-query hard-fail) and
all eight Round-2 suggestions are confirmed correctly applied; Lens B verified no
regressions from those fixes (GDALWarp handle ownership, RAII cleanup on all
error paths, atomic registry/sidecar writes intact). Lens B produced no findings.

### Findings
- [x] (must-fix) `MarineVerticalDatumProvider` is constructed outside any try
  block (main's `try` starts at :194), but its ctor throws on grid-setup failure
  (`vdatum_provider.cpp:46`) and `load_datum_config` can throw (`:51`) — a
  mistyped/missing `--geoid`/`--vdatum-grids`/`--datum-config` aborts via
  `std::terminate` (exit 134) instead of the clean stderr + `return 1` the
  sibling `--cell-size` (:114) and `constant:` (:172) paths already emit. Wrap it
  in the same try/catch — `src/s102_import_main.cpp:186`
- [x] (suggestion) `parseResolutionMeters` rejects `"nanm"` but accepts `"infm"`
  (`std::stod` → +inf passes the `'m'`/length/`>0` checks and would flow into
  `gggs::Level::fromCellSize`); add an `std::isfinite(value)` guard. Narrow —
  only a malformed catalog Resolution string (NOAA values trusted) —
  `src/s102/catalog.cpp:55`
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail remains
  doc-only, not code-enforced (`reference_writable` gates the layer, not
  costmap-liveness) — accepted at plan-review; code enforcement lands with #276.
  Carried, not a new code change for this PR. (deferred: tracking item, not a
  code change for this PR — code enforcement lands with #276)

### Next step
Lifecycle: **Local Review** → (verdict changes-requested) → **address-findings**
→ re-review. One must-fix + one suggestion are open above; the diff is not pushed
until a pre-push review returns **approved**. Hand off to a fresh-context
sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill address-findings

## Implementation
**Status**: complete
**When**: 2026-07-27 19:40 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-278 at `04b2c84`
**Addressed**: Local Review (Pre-Push) from 2026-07-27 19:34 +0000 (code state `d887822`)
**Commits**: e2f815f, 04b2c84

### Actions
- [x] (must-fix) Wrapped `MarineVerticalDatumProvider` construction in try/catch so
  a missing/mistyped `--geoid`/`--vdatum-grids`/`--datum-config` yields a clean
  stderr + `return 1` instead of `std::terminate` (exit 134), matching the sibling
  `--cell-size` and `constant:` paths — `marine_bathymetry_store/src/s102_import_main.cpp:186` (commit e2f815f)
- [x] (suggestion) Added `std::isfinite(value)` guard (plus `<cmath>`) in
  `parseResolutionMeters` so `"infm"` is rejected like `"nanm"` — `marine_bathymetry_store/src/s102/catalog.cpp:55` (commit 04b2c84)
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail (deferred: tracking
  item, not a code change for this PR — code enforcement lands with #276)

### Verification
ament_cpplint + ament_uncrustify clean ("No problems found") on both changed files.
Pre-commit hooks passed on each fix commit. No self-review — the next `review-code`
pass is the quality gate.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-27 19:52 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-278 at `8e45917` (code state `04b2c84`; HEAD is progress-only)
**Mode**: pre-push
**Depth**: Deep (reason: ~2.6k new lines, network fetch + GDAL warp/subprocess +
cross-layer vdatum wiring + costmap-feeding safety surface)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 4 | **Ship**: recommended — zero must-fix; both Round-3 fixes (vdatum
ctor try/catch in main, `isfinite` guard in parseResolutionMeters) verified
applied. Must-fix count 1→1→1→0 across rounds; the loop has converged. The two
remaining suggestions are low-value robustness (apply in a quick pass or track
and push).

Specialists: static analysis (ament_cpplint + ament_uncrustify "No problems
found" across all 15 changed C++ files; ament_cppcheck 2.13 not run — known-slow
self-skip, per prior rounds); Claude Adversarial 2 passes (Lens A logic/edge,
Lens B systemic/safety); Local Adversarial skipped (no Ollama server on :11434);
Copilot off (default). Both lenses independently found zero must-fix. Lens B
re-verified the prior-round fixes hold with no regressions (GDAL handle
ownership, RAII on all error paths, atomic writes, PRIVATE dep isolation).
Cross-pass confirmation: both lenses flagged the deferred-flush gap on the JSON
writers (suggestion 2). Several tempting-but-wrong issues considered and refuted
(path traversal via tile_id/URL — sanitizeComponent maps `/`→`_`; GDALWarp
double-free; CPLHTTPResult/CPLXMLNode/EVP_MD_CTX/VSILFILE leaks; PRIVATE dep
export leak; unescaped `prefix`/unbounded download — both unreachable behind the
documented S3-listing trust boundary and fixed compile-time constants) — not
carried here.

### Findings
- [x] (suggestion) `queryCatalog` doesn't guard empty `TILE_ID` — collapses the
  fetch cache-key to just the extension and the idempotency sidecar key to `""`,
  colliding two such rows (NOAA always populates it; low likelihood) — `src/s102/catalog.cpp:167`
- [x] (suggestion) `saveRegistryAtomic` / `saveImportedSidecarAtomic` check the
  open-stream failbit but not the deferred close-time flush before `fs::rename`;
  a disk-full at close could promote a truncated JSON (small writes, self-healing;
  `convert.cpp:236` does the explicit-flush opposite) — `src/s102/fetch.cpp:97`, `src/s102/run.cpp:69`
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail remains
  doc-only, not code-enforced — accepted at plan-review; code enforcement lands
  with #276. Carried, not a new code change for this PR. (deferred: tracking
  item — code enforcement is #276's scope; the scratch-only precondition stays
  documented in run.hpp / s102_import_main.cpp until it lands.)

### Next step
Lifecycle: **Local Review** (verdict approved) → push / open PR → **triage-reviews**

The verdict is **approved** (zero must-fix). The two open suggestions are optional
robustness — apply them in a quick `address-findings` pass or track and push. Once
pushed / a PR is opened, hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill triage-reviews

## Implementation
**Status**: complete
**When**: 2026-07-27 20:20 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-278 at `805f649`
**Addressed**: `## Local Review (Pre-Push)` (2026-07-27 19:52 +0000, branch `feature/issue-278` at `8e45917`) — Round 4 (approved)
**Commits**: `234d00d`, `805f649`

Round 4 returned **approved** (zero must-fix); its two open suggestions were the
optional robustness the review flagged as "apply in a quick pass or track and
push." Applied both here so nothing carries into the push. Built
`marine_bathymetry_store` clean and re-ran its suite after the fixes: 225 tests,
0 failures, 32 skipped. ament_cpplint + ament_uncrustify "No problems found"
across all three changed files. No pre-commit hook is installed in this worktree,
so linters were run manually; no `--no-verify` was used (nothing to bypass).

### Actions
- [x] (suggestion) `queryCatalog` now skips catalog rows with an empty `TILE_ID`
  (added to the existing url/sha256/Resolution guard), so a blank tile_id can no
  longer collapse the fetch cache-name to just the extension or the idempotency
  sidecar key to `""` and collide distinct rows — `marine_bathymetry_store/src/s102/catalog.cpp:167` (`234d00d`)
- [x] (suggestion) `saveRegistryAtomic` and `saveImportedSidecarAtomic` now
  `close()` the temp stream explicitly and re-check failbit before `fs::rename`,
  so a disk-full during the deferred close-time flush fails loud instead of
  promoting a truncated JSON over the good file (matching `convert.cpp:236`'s
  explicit-flush pattern) — `marine_bathymetry_store/src/s102/fetch.cpp:97`, `marine_bathymetry_store/src/s102/run.cpp:69` (`805f649`)
- [x] (governance-watch) ADR-0010 D7 scratch-stores-only guardrail (deferred:
  tracking item — code enforcement is #276's scope; the scratch-only precondition
  stays documented in run.hpp / s102_import_main.cpp until #276 lands)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off
to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-31 14:56 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-278 at `b542369`
**Mode**: pre-push
**Depth**: Light (reason: 2-commit confirmation pass, ~20 lines across 3 files; no security/cross-layer triggers)
**Must-fix**: 0 | **Suggestions**: 0
**Round**: 5 | **Ship**: recommended — both R4 suggestions resolved correctly; no must-fix, no regressions
**Local Adversarial**: off (--no-local, workspace#590 standing)

Round-5 confirmation focused on the two commits that resolved the R4 suggestions:
- `234d00d` — `queryCatalog` now adds `rec.tile_id.empty()` to the skip guard. Verified: `tile_id` is populated (catalog.cpp:160) before the guard (167); `GetFieldAsString` returns "" for null/missing so `.empty()` catches both. Comment rationale confirmed accurate — `fetch.cpp:195` keys the cache filename by `sanitizeComponent(tile_id)+ext` (empty → just extension) and `fetch.cpp:201`/`run.cpp:163` use `tile_id` as the registry/sidecar key (empty → "" collision). Purely additive filtering; no regression.
- `805f649` — `saveRegistryAtomic`/`saveImportedSidecarAtomic` now call `out.close()` before the `if (!out)` check, ahead of `fs::rename`. Verified ordering: write → close (surfaces deferred disk-full failbit) → check → throw-before-rename. The pre-existing "cannot open" path stays covered (the `<<` already sets failbit); the change only adds detection of the deferred close-time flush failure. No regression.

Static analysis: clean — longest changed-file lines 92/83/81 vs ament limit 99; no other lint concerns. Build + full suite reported 286/0 green after the fixes; worktree clean.

### Findings
- [ ] No issues found. LGTM.

### Next step
Lifecycle: **Local Review** (approved) → push / open PR → **triage-reviews**.
Branch is shippable at `b542369`. Hand off to a fresh-context sub-agent after push:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill triage-reviews

## Integrated Review
**Status**: complete
**When**: 2026-07-31 11:30 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #286 at `3eb585f`
**Sources**: 3 (Copilot R1 @ `3eb585f`, Local Review rounds 1-5 @ `3eb585f` and prior, CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass (`build` success, `copilot-pull-request-reviewer` success)

### Findings
- [x] (low, Copilot) `--cell-size` accepts non-positive / non-finite values (`0`,
  `-1`, `nan`, `inf` all parse via `std::stod`) which flow into
  `BathymetryStore::fromCellSize()` → `gggs::Level::fromCellSize()` where
  `std::log2` of 0/negative yields inf/NaN and the subsequent
  `static_cast<int>` is undefined behavior (`gggs/level.h:67`). Verified
  valid against current code: the R2 hardening pass (`dcded4e`) added
  non-finite rejection for `--area` and a try/catch for non-numeric
  `--cell-size`, but no range/finiteness check on the parsed value. Fix:
  reject `!std::isfinite(v) || v <= 0` at parse time in
  `src/s102_import_main.cpp:112-121`, matching the `--area` validation
  pattern. Note: `import_geotiff_main.cpp:141` has the same pre-existing
  exposure (out of this PR's scope — candidate follow-up).

### False positives
- none — the single Copilot comment verified valid.

### Next step
Lifecycle: **Integrated Review** → `address-findings` (1 open finding) →
`review-code` re-review → merge checkpoint.

## Implementation
**Status**: complete
**When**: 2026-07-31 15:35 +00:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #286 at `f4e20a1`
**Addressed**: `## Integrated Review` (2026-07-31 11:30 -04:00, PR #286 at `3eb585f`) — 1 open finding
**Commits**: `f4e20a1`

The single open Copilot finding, verified valid against current source before
acting: the `--cell-size` try/catch (`s102_import_main.cpp:112-121`) rejected only
non-numeric input; `std::stod` still accepted `0`/`-1`/`nan`/`inf`, which flowed
into `gggs::Level::fromCellSize()` where `std::log2(<=0)` yields inf/NaN and the
following `static_cast<int>` is UB. Added a finiteness+positivity guard right after
the parse, mirroring the existing `--area` finiteness check in the same file.

### Actions
- [x] (low, Copilot) Reject non-positive / non-finite `--cell-size` at parse time
  (`!std::isfinite(v) || v <= 0` → clean stderr + `return 1`), matching the `--area`
  validation pattern — `marine_bathymetry_store/src/s102_import_main.cpp:114` (`f4e20a1`)

### Verification
Built `marine_bathymetry_store` clean (`colcon build`, Release) — the change
compiles; only pre-existing unrelated `-Wunused-result` warnings in test fixtures.
ament_cpplint + ament_uncrustify "No problems found" on the changed file. Pre-commit
hooks passed on the fix commit; no `--no-verify`. The CLI parser lives in `main()`
(not unit-tested); the guard reuses the already-compiled `--area` `std::isfinite`
pattern. No self-review — the next `review-code` pass is the quality gate.

Note: Copilot flagged the same pre-existing exposure at `import_geotiff_main.cpp:141`
as out of this PR's scope (candidate follow-up) — not addressed here.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fix). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 278 --skill review-code
