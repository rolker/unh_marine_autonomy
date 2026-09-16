# Plan: Depth overview pyramids go stale — detect it and refold incrementally

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/389

## Context

`build_depth_overviews` (ADR-0011) rebuilds a depth layer's `overviews/`
sidecar wholesale, every time, with nothing to tell a stale sidecar from a
current one and nothing cheaper than a full refold to repair it. On
2026-09-15 the dev-host `depths/processed/overviews/` was a fold of a store
regenerated a week earlier and still carried removed blunders; CAMP drew it
as truth. [ADR-0011 §6](../../../docs/decisions/0011-overview-pyramid.md#decision)
(amended 2026-09-16, commits `f046d79` and `822d3da`) already pins the
contract this plan implements: a **source catalog** (`overviews/source.json`,
schema `overview-source/1`), a **staleness check** (`--check` → exit
0/3/4/5/6), an **`--if-stale`** trigger mode, and an **incremental refold**
that re-folds only a changed native tile's own `(level, index)` and its
ancestors, carrying every other derived tile over unchanged. This plan is
implementation only — the design is settled; nothing here reopens it.

Decided by the operator (`.agent/work-plans/issue-389/progress.md`, Issue
Review, resolved):
- Staleness basis = the native-catalog digest (§6), not a `cube_bathymetry`
  content fingerprint.
- Incremental fold is **in scope** for this PR.
- The ADR amendment is done first — it is (`f046d79`, `822d3da`).
- The trigger call site (`build_bathy_store.sh` running `--if-stale` after
  every ingest) is
  [rolker/unh_echoboats_project11#490](https://github.com/rolker/unh_echoboats_project11/issues/490) —
  **out of scope here**; that issue is blocked on this PR landing, not the
  reverse.
- The MBES backscatter builder is a separate issue
  ([#390](https://github.com/rolker/unh_marine_autonomy/issues/390)) —
  **out of scope here**. `build_sidescan_overviews` keeps building unchanged;
  it and the future MBES builder adopt the new engine pieces when they next
  change (ADR-0011 §6, last incremental-refold paragraph).
- "Rebuild the dev-host `processed` pyramid now" is a **post-merge
  operational step**, not part of this PR's test plan.

### Current shape of the code

- `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp`
  / `src/overview_pyramid.cpp` (715 lines) — `parseDepthOverviewArgs`,
  `buildDepthOverviewPyramid` (level discovery via `scanCoverage`, per-level
  `buildLevel` fold with native-wins suppression, crash-safe
  `renameat2`/rename-aside swap into `overviews/`). Wholesale only today: every
  run folds every level from `finest - 1` down to `min_level`.
- `marine_bathymetry_store/src/build_depth_overviews.cpp` — thin CLI: parses
  args, calls `buildDepthOverviewPyramid`, maps `tiles_skipped > 0` to exit 4,
  otherwise 0 (1 on exception, 2 on usage).
- `marine_tiled_raster_store/include/marine_tiled_raster_store/overview_builder.hpp`
  — generic per-cell fold engine (`buildParentTile`), policy-agnostic.
- `marine_tiled_raster_store/include/marine_tiled_raster_store/coverage_manifest.hpp`
  / `src/coverage_manifest.cpp` — `CoverageManifest` (mixed-level `(level,
  index)` set + optional per-tile geometric error), `coverage-manifest/1` JSON
  I/O (atomic tmp+rename, **not** the sidecar's own crash-safe swap — this file
  already lives inside `overviews.tmp/` before that swap happens), directory
  scan (`scanCoverage`/`gridsInDir`/`gridFromTileName`).
- `marine_tiled_raster_store/include/marine_tiled_raster_store/tile_io.hpp` /
  `src/tile_io.cpp` — `saveTile`/`loadTile`; `saveTile`'s GDAL `Create()` always
  truncates in place (no in-place partial write), which is why carry-over must
  never target a path `saveTile` might also write to instead of writing a
  separate carried copy.
- `marine_sidescan_mosaic/src/overview_pyramid.cpp` — separate, single-level
  builder; **must keep building unchanged** (no `--check`/`--if-stale`, no
  catalog). It does not use `marine_bathymetry_store` at all, only
  `marine_tiled_raster_store`, so it is unaffected as long as the new engine
  pieces are additive.
- Tests: `marine_bathymetry_store/test/test_depth_overview.cpp` (36 cases,
  golden-fixture pin `SingleLevelRegression`), `marine_tiled_raster_store/test/{test_coverage_manifest.cpp,test_overview_builder.cpp}`.
- `marine_bathymetry_store/README.md` §"Depth overview pyramids" (~line 234).

## Approach

1. **Shared engine: source catalog (`marine_tiled_raster_store`).** New
   `include/marine_tiled_raster_store/source_catalog.hpp` +
   `src/source_catalog.cpp`:
   - `struct SourceEntry { std::string name; uintmax_t size; int64_t
     mtime_ns; };` — one native `*.tif` file directly in the layer dir
     (regular files only, **any** name — a mis-named tile is still a change
     per §6).
   - `struct SourceCatalog { std::vector<SourceEntry> entries; std::string
     digest; std::string policy_version; int min_level; std::vector<uint8_t>
     levels_built; };` `entries` sorted by name (matches
     `CoverageManifest`'s existing sort-by-key convention).
   - `SourceCatalog buildSourceCatalog(const std::string & layer_dir)` — a
     **stat pass only** (`fs::directory_iterator` + `fs::file_size` +
     `fs::last_write_time` converted to nanoseconds since epoch via
     `file_clock::to_sys` → `time_since_epoch`), no tile open, no GDAL. Does
     **not** set `digest`/`policy_version`/`min_level`/`levels_built` — the
     caller (the depth builder) fills those in after the pass, matching the
     ADR's "taken after the last native tile has been read" timing
     requirement; the *helper* just stats, the *caller* decides when to call
     it.
   - `std::string computeDigest(const std::vector<SourceEntry> & entries)` —
     the digest preimage is `"<name>\t<size>\t<mtime_ns>\n"` per entry,
     **entries pre-sorted by name**, concatenated in order, hashed with
     **FNV-1a 64-bit** (no new dependency; this is a change-detector, not a
     security boundary — same posture as `coverage.json`'s advisory status).
     Printed as 16 lowercase hex digits. Deciding this now closes the ADR's
     "digest's exact preimage" mechanism gap.
   - `void saveSourceCatalog(const SourceCatalog &, const std::string & path)`
     / `std::optional<SourceCatalog> loadSourceCatalog(const std::string &
     path)` — schema tag `overview-source/1`, same tolerant-read /
     atomic-tmp-rename posture as `saveCoverageManifest`/`loadCoverageManifest`
     (missing/malformed/unknown-schema → `nullopt` + `std::cerr` warning,
     never throws).
   - `inline const char * sourceCatalogFilename() {return "source.json";}`
   - `struct CatalogDiff { std::vector<std::string> added, removed, changed;
     };` `CatalogDiff diffCatalogs(const SourceCatalog & recorded, const
     SourceCatalog & current)` — name-keyed diff; `changed` = same name,
     different `(size, mtime_ns)`. (The same-size-same-mtime-within-a-second
     blind spot is inherent here, not a bug — documented in the header
     verbatim from ADR §6.)

2. **Shared engine: dirty-ancestor walk, sidecar-intact check, carry-over
   (`marine_tiled_raster_store`).** New
   `include/marine_tiled_raster_store/incremental_overview.hpp` +
   `src/incremental_overview.cpp`:
   - `std::set<gggs::GridIndex> dirtyAncestors(const gggs::GridIndex & changed,
     uint8_t min_level)` — `changed`'s own grid plus every `gggs::parent()`
     step up to and including `min_level` (stops when `parent()` returns an
     invalid grid or `level() <= min_level`). Pure, keyed on `gggs::parent()`
     so it needs no polar-band special case (ADR §6).
   - `std::set<gggs::GridIndex> computeDirtySet(const std::vector<gggs::GridIndex>
     & changed_native_grids, uint8_t min_level)` — union of `dirtyAncestors`
     over every changed grid. Callers derive `changed_native_grids` from a
     `CatalogDiff`'s `added ∪ removed ∪ changed` names via
     `gridFromTileName` (skip-and-warn on a name that no longer reconstructs,
     same posture as the rest of this file).
   - `bool sidecarIntact(const std::string & overviews_dir, const
     CoverageManifest & recorded)` — the `.tif` set actually present in
     `overviews_dir` (via `gridsInDir(overviews_dir, std::nullopt, skipped)`,
     `skipped` must be 0) equals `recorded`'s full grid set exactly (both
     directions — a hand-deleted tile *and* a stray extra tile are both
     damage).
   - `enum class CarryOverResult { kLinked, kCopied };` `CarryOverResult
     carryOverTile(const std::string & src_path, const std::string &
     dst_path)` — `fs::create_hard_link(src, dst)`; on any failure (`EXDEV`
     across filesystems, or a filesystem without hard-link support — NFS
     mounts support them, but the ADR's "Filesystems" paragraph reserves the
     right for a mount that does not) falls back to `fs::copy_file`. **Never
     writes through the result**: this function only ever creates `dst_path`
     fresh in the (not-yet-live) staging directory; nothing later opens
     `dst_path` for write. The load-bearing invariant this depends on is
     staged separately: the fold loop must never target a carried tile's
     staging path with `saveTile` (see step 4) — `saveTile`'s GDAL `Create()`
     truncates in place, so a write to a hard-linked path would corrupt the
     *previous* sidecar's tile too. This is exactly why the dirty and
     carried sets must be disjoint by construction (ADR §6), and step 4
     enforces that by construction (dirty grids are computed first and
     carry-over explicitly skips them) rather than by a runtime check alone.

3. **Depth builder: policy version + catalog wiring
   (`marine_bathymetry_store`).**
   - `overview_pyramid.hpp`: add `constexpr const char * kDepthPolicyVersion =
     "depth-shallowest/1";` (a named constant, not a magic string, so a future
     fold-rule change has one place to bump per the ADR's "the depth builder
     bumps whenever its fold or error-saturation rules change"). Extend
     `DepthOverviewOptions` with `enum class Mode { kBuild, kDryRun, kCheck,
     kIfStale, kFull };` replacing the current `bool dry_run` (dry-run becomes
     `Mode::kDryRun`; default `Mode::kBuild` preserves today's behavior
     exactly). Add `struct DepthStalenessReport { enum class Status { kCurrent,
     kStale, kUnknown, kBusy } status; std::vector<std::string> added_names,
     removed_names, changed_names; std::string reason; };` (`reason` covers
     unknown/busy explanations and the sidecar-damage case, which is also
     `kStale`).
   - New entry point: `DepthStalenessReport checkDepthOverviewStaleness(const
     std::string & layer_dir, int min_level)`. Order of checks (all read-only,
     no lock claimed — `--check` must never create `overviews.tmp/`):
     1. `overviews.tmp/` exists → `kBusy`.
     2. `overviews/source.json` or `overviews/coverage.json` missing/unreadable
        → `kUnknown`.
     3. Recorded `policy_version != kDepthPolicyVersion` or recorded
        `min_level != min_level` → `kStale` (`reason` names which changed;
        these always force a **full** fold per step 4, never incremental).
     4. `!sidecarIntact(...)` → `kStale`, `reason = "sidecar damaged"`.
     5. Recompute `buildSourceCatalog(layer_dir)`; compare digests first
        (cheap short-circuit for the common current case), then fall back to
        `diffCatalogs` for the added/removed/changed lists when digests
        differ → `kStale` with those lists populated, or `kCurrent` when the
        digest (and therefore the full comparison) matches.
   - `build_depth_overviews.cpp` maps `Status` to the exit codes: `kCurrent`
     → 0 (one line, per ADR "a no-op ... exit 0, one line"); `kStale` → 3
     (prints added/removed/changed counts + first few names, or the sidecar
     damage reason); `kUnknown` → 5; `kBusy` → 6. `--if-stale` runs the same
     check first; on `kCurrent` it is the same one-line no-op/exit-0; on
     `kStale`/`kUnknown` it proceeds to build (incremental when the report
     came from step 5 alone with no policy/min-level/sidecar-damage cause;
     full otherwise) and returns the **build's own** exit codes (0/4), not
     3/5/6 — matching the ADR's "`--if-stale` ... returns the build's own
     codes" line. `6` (busy) always wins for `--if-stale` too: a concurrent
     build must refuse, not race the lock.

4. **Depth builder: incremental refold path.** Refactor
   `buildDepthOverviewPyramid`:
   - Compute up front whether this run is **eligible for incremental**: the
     `checkDepthOverviewStaleness` preconditions from step 3 (§6: both files
     readable, policy version + min_level match, sidecar intact) **and**
     `opts.mode != Mode::kFull`. `Mode::kDryRun`/`Mode::kBuild` direct runs are
     always full (today's behavior, unchanged) — only `Mode::kIfStale` (and a
     future explicit `--incremental`, not exposed yet — YAGNI'd per the ADR
     text which only asks for `--if-stale` and `--full`) takes the
     incremental path. Log which path was taken and why, one line, same style
     as the existing per-level progress lines.
   - **Incremental path:**
     a. Recompute the native catalog + `CatalogDiff` (reuse the comparison
        from the staleness check rather than doing it twice — thread the
        already-computed `DepthStalenessReport` through, or recompute; prefer
        threading it through to keep `--if-stale`'s single check authoritative
        for both the "should I build" decision and "what's dirty").
     b. `changed_native_grids` = `gridFromTileName` over
        `added ∪ removed ∪ changed` (skip-and-warn on failures, same as
        `gridsInDir`).
     c. `dirty = computeDirtySet(changed_native_grids, min_level)`.
     d. Claim `overviews.tmp/` (same lock as today).
     e. **Carry over first, disjoint from dirty, before any dirty tile is
        written** (ADR §6 ordering requirement): for every grid in the
        previous `coverage.json` **not** in `dirty`, `carryOverTile` its
        `.tif` from `overviews/` into `overviews.tmp/`, and seed the new
        `CoverageManifest` with that grid's carried geometric error verbatim.
        A carried grid whose previous manifest entry has **no** recorded
        error is treated as **damage** and forces a fallback to a full fold
        for this run (ADR §6: "a previous manifest missing a carried tile's
        error is treated as damage and forces a full fold") — implemented as
        a fallback flag checked before step (f), not a thrown exception (a
        full fold is a valid outcome, not an error).
     f. Fold **only** the dirty `(level, index)` slots, level by level from
        `finest - 1` (or the shallower of `finest - 1` and the deepest dirty
        level — dirty slots cannot exist below the finest native level) down
        to `min_level`, reusing `buildLevel` but restricted to `by_parent`
        groups whose key is in `dirty` at that level. A dirty parent with
        **zero** contributors after the native+derived-children union
        (because its only-ever native child tile was removed and nothing
        replaced it, and no sibling contributes either) is **deleted**: not
        written to staging, not added to the derived manifest, and if a
        stale copy was carried over from a *different* dirty ancestor's
        recompute it is removed from staging. A dirty slot now occupied by a
        native tile is dropped from the derived output entirely (native
        wins, §2 #331 — unchanged).
     g. New `coverage.json` = carried ∪ refolded − deleted, written into
        staging exactly as today (step order unchanged from the existing
        wholesale path).
     h. **Post-fold re-stat drift refusal**: immediately before the swap,
        recompute `buildSourceCatalog(layer_dir)` one more time and compare
        its digest to the one taken at step (a)/entry. A mismatch means an
        importer rewrote a native tile while this run was folding (the ADR's
        multi-hour-run race) — refuse the swap (clean up staging, return a
        result whose caller-visible status is the refusal, distinct from
        `tiles_skipped` but reported the same way: non-zero exit, previous
        sidecar untouched) rather than publish a sidecar the catalog will
        immediately call stale again on the next `--if-stale`. This check
        applies to **both** the incremental and full-fold paths — it is a
        general "catalog moved under us" guard, not incremental-specific,
        but it can only exist once the catalog exists at all, hence landing
        in this step.
     i. Swap as today (§2 mechanism unchanged) — **plus** write the fresh
        `source.json` into staging next to `coverage.json` (both ride the
        same swap, per ADR §6 "rides the same swap and is crash-consistent").
   - **Full path** (`Mode::kBuild`/`kDryRun`, or incremental ineligible, or
     the damaged-carry-error fallback from (e)): today's loop, unchanged,
     plus writing `source.json` into staging alongside `coverage.json`
     (this part applies regardless of incremental eligibility — every
     wholesale run from here on records a catalog, which is what makes the
     *next* run's staleness check possible; today's sidecars have none,
     which is the documented migration case).
   - `DepthOverviewBuildResult` gains `bool incremental = false;` (which path
     ran, for the CLI's success message) and `bool refused_stale_input =
     false;` (the step-h drift refusal, mapped to the same exit path as
     `tiles_skipped > 0` — both are "refused, previous sidecar untouched").

5. **CLI surface (`build_depth_overviews.cpp`).** Add `--check`, `--if-stale`,
   `--full` to `parseDepthOverviewArgs` (mutually exclusive with each other
   and with `--dry-run`; `--full` and `--min-level`/nothing else combine
   normally). Wire the five-way exit-code mapping from step 3. Update the
   `usage()` text.

6. **Tests** — see the Consequences-driven list below; every case names its
   file and, where new, a suggested test name.

7. **Documentation** — `marine_bathymetry_store/README.md` §"Depth overview
   pyramids" gets a new subsection documenting `source.json`, `--check`
   (with the exit-code table), `--if-stale`, `--full`, and the incremental
   refold's scope (dirty ancestors + carry-over), at the same level of detail
   as the existing bullets. `docs/decisions/0011-overview-pyramid.md` §2 gets
   a one-line cross-reference next to the existing "wholesale ... builders
   delete and recreate it wholesale" sentence, mirroring the pattern already
   used there for the #331 amendment (a parenthetical "([#389](...)) now
   folds only the dirty subset when eligible — see §6" pointer, not a
   rewrite of §2's own text, which §6 already states stays correct as
   "the live sidecar is replaced whole, not every tile is recomputed").

## Files to Change

| File | Change |
|------|--------|
| `marine_tiled_raster_store/include/marine_tiled_raster_store/source_catalog.hpp` | New. `SourceEntry`, `SourceCatalog`, `buildSourceCatalog`, `computeDigest`, `save/loadSourceCatalog`, `sourceCatalogFilename`, `CatalogDiff`, `diffCatalogs`. |
| `marine_tiled_raster_store/src/source_catalog.cpp` | New. Implementation: stat pass, FNV-1a digest, `overview-source/1` JSON I/O (mirrors `coverage_manifest.cpp`'s tolerant-read / atomic-write pattern). |
| `marine_tiled_raster_store/include/marine_tiled_raster_store/incremental_overview.hpp` | New. `dirtyAncestors`, `computeDirtySet`, `sidecarIntact`, `carryOverTile`/`CarryOverResult`. |
| `marine_tiled_raster_store/src/incremental_overview.cpp` | New. Implementation. |
| `marine_tiled_raster_store/CMakeLists.txt` | Add both new `.cpp` to `add_library`; add `test_source_catalog` and `test_incremental_overview` gtest targets. |
| `marine_bathymetry_store/include/marine_bathymetry_store/overview_pyramid.hpp` | `kDepthPolicyVersion`; `DepthOverviewOptions::Mode` replacing `bool dry_run`; `DepthStalenessReport`; `checkDepthOverviewStaleness` declaration; `DepthOverviewBuildResult::incremental` + `refused_stale_input`. |
| `marine_bathymetry_store/src/overview_pyramid.cpp` | `checkDepthOverviewStaleness` implementation; incremental-eligibility decision; incremental fold path (carry-over-first, dirty-only fold, deletion handling); post-fold re-stat drift refusal (both paths); `source.json` write into staging (both paths). |
| `marine_bathymetry_store/src/build_depth_overviews.cpp` | `--check`/`--if-stale`/`--full` parsing (via the `Mode` enum) and the exit-code mapping (0/3/4/5/6); usage text. |
| `marine_bathymetry_store/CMakeLists.txt` | Link the two new `marine_tiled_raster_store` translation units transitively (already pulled in via the `marine_tiled_raster_store` target — verify no new explicit list entry is needed since it's a library dependency, not a source of this package). |
| `marine_bathymetry_store/test/test_depth_overview.cpp` | New `TEST` cases for `--check` (current/stale-added/stale-removed/stale-changed/stale-damaged-sidecar/unknown/busy), `--if-stale` (no-op and build), incremental-equals-full equivalence (byte-for-byte tile comparison), carry-over-never-writes-through-link, geometric-error saturation over carried children, post-fold re-stat drift refusal, policy-version-change and min-level-change forcing full. |
| `marine_tiled_raster_store/test/test_source_catalog.cpp` | New. Round-trip (build → save → load → equals), digest stability (same inputs in different directory-iteration order → same digest, since entries are sorted before hashing), `diffCatalogs` added/removed/changed classification, the documented same-size-same-mtime-within-resolution blind spot (explicit test asserting it is *not* detected — pins the documented limitation so a future "fix" doesn't silently change the contract). |
| `marine_tiled_raster_store/test/test_incremental_overview.cpp` | New. `dirtyAncestors`/`computeDirtySet` correctness (single change, multiple changes with shared ancestors, a change at `min_level` itself), `sidecarIntact` (matches, extra file, missing file), `carryOverTile` (hard-link case + forced-copy-fallback case via a stubbed/permission path, or documenting the `EXDEV` path is untested on a single-filesystem CI runner and covered by the depth-builder integration test instead). |
| `marine_bathymetry_store/README.md` | New content under §"Depth overview pyramids": `source.json`, `--check` exit-code table, `--if-stale`, `--full`, incremental-refold scope. |
| `docs/decisions/0011-overview-pyramid.md` | One-line §2 cross-reference to §6, per Approach step 7. |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Verify against source, not assumptions | Read `overview_pyramid.{hpp,cpp}`, `overview_builder.hpp`, `coverage_manifest.{hpp,cpp}`, `tile_io.{hpp,cpp}`, existing tests and README before writing this plan; every file/function named above was confirmed to exist (or confirmed absent, for the new files) by reading it. |
| Fix completely, no "good enough" | The plan includes the drift-refusal guard, the deletion case for a dirty slot with zero contributors, and the damaged-carry-error fallback — not just the happy path. |
| Shared engine vs. per-store policy (ADR-0011 §4) | Catalog, dirty-set walk, sidecar-intact check, and carry-over land in `marine_tiled_raster_store` (step 1–2); only the policy version string and the CLI modes are depth-specific (step 3–5) — matches the operator's resolved action item and the ADR's explicit "the depth builder is the first adopter" line. |
| Safety / advisory-only (ADR-0013 D8) | Nothing in this plan touches a query path; `checkDepthOverviewStaleness` and the new sidecar fields are read only by the CLI and (later, separately) a display consumer. Called out explicitly in the staleness-check section. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0011 §6 (this amendment) | Yes | This plan *is* the implementation of §6; every mechanism (catalog schema, exit codes, dirty-set ordering, carry-over safety, drift refusal) is taken verbatim from the accepted text, not reinterpreted. |
| ADR-0011 §2 (swap, wholesale language) | Yes | Unchanged mechanism reused for the new `source.json` (rides the same swap); one-line cross-reference added, no rewrite (§6 already states the "wholesale" reading holds). |
| ADR-0011 §4 (fold policies per-store, engine shared) | Yes | Followed: incremental machinery is store-agnostic in `marine_tiled_raster_store`; the depth fold policy itself (`depthShallowestFold`) is untouched. |
| ADR-0013 D1/D2/D3 (geometric error, coverage manifest) | Yes | Carried-tile error is seeded verbatim before folding (saturation preserved); a carried tile with no recorded error is treated as damage, per §6. |
| ADR-0013 D8 (advisory-only, no safety-path read) | Yes | No new code reads the sidecar/catalog from a query path; explicitly checked in the Principles row above. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `DepthOverviewOptions` (bool → Mode enum) | Every caller of `buildDepthOverviewPyramid`/`parseDepthOverviewArgs` — only `build_depth_overviews.cpp` and the test file | Yes — both listed in Files to Change |
| The wholesale fold path now also writes `source.json` | `test_depth_overview.cpp`'s existing `EndToEndSidecarSwapIdempotentAndLocked` and `SingleLevelRegression` golden test | Yes — golden fixture compares *tile* bytes, not the sidecar's file listing, so it should be unaffected; explicitly verify this when implementing (flagged, not blindly assumed) |
| New engine files in `marine_tiled_raster_store` | `marine_tiled_raster_store/CMakeLists.txt` (library sources + new test targets) | Yes |
| `marine_bathymetry_store/README.md` | Nothing else references this section by line number in-repo (checked: only the ADR's own Consequences bullet references it, and that's prose, not a line anchor) | Yes — README updated; no other doc to touch |
| `build_bathy_store.sh` trigger wiring | `rolker/unh_echoboats_project11#490` | **No** — different repo, explicitly out of scope, blocked on this PR's interface landing |
| MBES backscatter builder adopting the new engine | `#390` | **No** — separate issue, explicitly out of scope; the engine pieces are additive so #390 needs no coordination beyond "land after this merges" |
| `marine_sidescan_mosaic`'s own overview builder | Nothing — it doesn't use `marine_bathymetry_store` and isn't required to adopt the new engine pieces now | Confirmed no change needed; verified by reading `marine_sidescan_mosaic/src/overview_pyramid.cpp`'s dependency list (only `marine_tiled_raster_store`, and only the pre-existing `overview_builder.hpp`/`tile_io.hpp` pieces) |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `marine_bathymetry_store/README.md`
  §"Depth overview pyramids" (new `source.json`/`--check`/`--if-stale`/`--full`
  content) and `docs/decisions/0011-overview-pyramid.md` §2 (one-line
  cross-reference to §6) — both listed in Files to Change.
- **Agent-instruction candidates** (proposals only — operator decides): None
  identified. This is a single-repo, single-package mechanism change with no
  new workflow pattern (worktree usage, git identity, CI gating) that would
  belong in `.agent/knowledge/` or `AGENTS.md`.

## Open Questions

- [ ] None that block implementation — the ADR's three deliberately-deferred
  mechanism choices (digest preimage, link-vs-copy carry-over, report format)
  are settled in Approach steps 1–3 above. Flagging one judgment call for
  review-plan/implementation-time confirmation rather than as a blocker: step
  4(h)'s post-fold re-stat drift refusal is specified to fire on **both** the
  incremental and full-fold paths (a strict reading of §6 only requires it
  where the catalog exists to compare against, which is now always, since
  step 4 makes every wholesale run write a catalog too) — if that reading is
  wrong, narrowing it to incremental-only is a small, isolated change.

## Estimated Scope

Single PR. Two new files in `marine_tiled_raster_store` (~150–250 lines
combined, engine-level and store-agnostic), a substantial but contained
rewrite of `marine_bathymetry_store/src/overview_pyramid.cpp`'s build
function (the existing wholesale loop becomes one of two paths sharing the
per-level `buildLevel` helper), a small CLI surface change, and README/ADR
doc updates. Test volume is the largest single piece — roughly 15–20 new
`TEST` cases across three files — reflecting the "explicit tests for" list in
the issue-review action items and the sub-agent brief.

**Storage/time estimate against the dev-host layer** (`~/data/world/depths/processed`,
69 native L10 tiles, 167 MB, per the 2026-09-15 incident): the source catalog
is a stat pass over ~69 files — milliseconds, independent of tile content
size, matching the ADR's "what lets the check run unconditionally after every
import." A single-tile change's dirty set is at most `10 - min_level`
ancestor levels (`min_level` defaults to 0, so ≤10 tiles re-read/re-folded
vs. the full pyramid's ~23 tiles at a 4:1 fold ratio down from 69 — roughly a
2–3x reduction for a one-tile edit, growing toward the full pyramid's cost
only as the fraction of changed native tiles grows). Carry-over of the
remaining ~13–20 derived tiles is a hard-link per file (no data copy) on the
dev host's local ext4 filesystem, so incremental refold cost is dominated by
the dirty-tile GeoTIFF reads/writes, not by carry-over I/O — the storage
question the ADR's "Filesystems" paragraph flags (NFS/FUSE forcing a copy
instead of a link) does not apply to this layer's location.
