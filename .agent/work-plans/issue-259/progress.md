---
issue: 259
---

# Issue #259 — Survey Indexer + Query CLI

## Issue Review
**Status**: complete
**When**: 2026-07-13 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #259
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Stage 1 of the #258 (survey data exploration) umbrella. Deliverables: an offline SQLite survey indexer and a query CLI. Scope is appropriately bounded — no GUI, no viewer integration, no CUBE re-runs deferred to later stages.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Offline tool with human-readable + `--json` output; regenerable sidecar (bags remain data of record) |
| Enforcement over documentation | Watch | If sensor topic names or SQLite schema become a convention, document them in the knowledge file and enforce via a schema version check |
| Capture decisions, not just implementations | Action needed | Three open decisions (package home, exact schema, TF pattern reuse vs re-implement) must produce a plan-phase ADR or recorded decision — plan-task should capture them |
| A change includes its consequences | Action needed | No mention of automated tests; the acceptance sketch is manual. An automated smoke test for the indexer and CLI should be part of the deliverables |
| Only what's needed | OK | Non-goals are explicitly staged to #258 stages 2–5; scope is minimal |
| Improve incrementally | OK | Correctly staged as step 1 of 5 |
| Test what breaks | Watch | Query correctness (wrong tile match, ping-in-gap misidentified) is high-value to test; coverage-chase is not needed but at least one fixture-based regression test is expected |
| Workspace vs. project separation | OK | Belongs in a project repo (`unh_marine_autonomy` or `marine_tools`); no workspace leakage |
| Workspace improvements cascade | N/A | Project-specific tooling |
| Primary framework first | OK | No framework concerns raised |

### Project Principles

| Principle | Status | Notes |
|---|---|---|
| Safety First | N/A | Offline tool, no control path |
| Hardware Agnosticism | Watch | Issue names specific topic namespaces (M3 via kongsberg_em_bridge, Garmin GCV). The `sensor_type` field should use ADR-0005 vocabulary (`sidescan`, `mbes-bathy`) so adding a future sensor doesn't require a schema migration |
| Modularity and Decoupling | Watch | Package placement affects the dependency graph; plan-task should decide based on which existing package already has the GGGS + store dependency (lean toward `unh_marine_autonomy` next to `marine_tiled_raster_store`) |
| Simulation-First | N/A | Offline post-processing, no runtime behavior |
| Iterative, Validated Evolution | OK | Staged correctly |
| Standards Compliance | Watch | If a new ROS 2 package is created, ADR-0008 requires ROS 2 naming / `package.xml` / license headers; also check REP-144 for package naming |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 (Adopt ADRs) | Yes | Open decisions on schema and package home need to be recorded — plan-task should produce a decision record, not just implementation |
| ADR-0002 (Worktree isolation) | Yes | Already in worktree; continue as required |
| ADR-0003 (Project-agnostic workspace) | No | Goes in project repo — correct |
| ADR-0005 (Provenance registry) | Yes | Issue says to align `sensor_type` with ADR-0005 provenance registry ids "where practical." ADR-0005 D3 defines `sensor_class` vocabulary (`sidescan`, `mbes-bathy`, `mbes-backscatter`). The SQLite passes table's `sensor_type` column should use these values exactly; this is cheap insurance against a later schema migration when the second sensor/platform arrives |
| ADR-0008 (ROS 2 conventions) | Yes | If a new ROS 2 package is created, naming, `package.xml`, and license headers must follow ROS 2 conventions |
| ADR-0013 (progress.md vocabulary) | Yes | Subsequent phases must use correct entry types |

### Consequences

- `docs/sonar_ecosystem.md` is missing a "Survey indexer / query" row for #258/#259. The **Reprocess** row covers offline M3 bag → store, but the indexer is a distinct capability (it indexes *without* requiring store acceptance — pings that missed CUBE still appear). Add a row when the deliverable lands.
- The SQLite schema is a cross-stage data contract — stages 2–5 of #258 will consume it. It should be documented in the plan (not just in code comments) so future stages have a stable surface to build against.
- If a new package is created: `package.xml`, `CMakeLists.txt` (or `setup.py`), and license headers must be added per ADR-0008 and existing unh_marine_autonomy package conventions.

### Recommendations

- **Package home**: lean toward a new package inside `unh_marine_autonomy` (next to `marine_tiled_raster_store`), because GGGS and the store architecture already live there; `marine_tools` would create an inward dependency that `marine_tools` currently doesn't carry. Confirm in plan-task.
- **Reuse the TF pattern from cube#63 / sidescan_mosaic_bag.cpp** (`marine_sidescan_mosaic/src/sidescan_mosaic_bag.cpp`). cube#63 is closed (M3 import landed). Plan-task should reference these implementations explicitly rather than designing a new TF walk.
- **SQLite schema alignment with ADR-0005**: use `sensor_class` values from ADR-0005 D3 (`sidescan`, `mbes-bathy`) as the `sensor_type` vocabulary; record `platform` and `sensor` (model) in the bags table or a registry sidecar — consistent with `StoreMetadata` from the ADR-0005 #248 amendment.
- **Add a fixture-based test**: at minimum, a test that builds a minimal mock bag with known pings, runs the indexer, and verifies the query CLI returns the expected tile/time-window result. The acceptance sketch (point at Massabesic) cannot serve as a regression test.
- **Document the schema** in the plan ADR or a `docs/` file so stages 2–5 of #258 have a stable interface description without reading the implementation.

### Actions
- [ ] Capture open decisions (package home, SQLite schema, TF reuse) in a plan-phase ADR or recorded decision (not just code)
- [ ] Add at least one automated fixture-based test for the indexer and query CLI
- [ ] Align `sensor_type` column vocabulary with ADR-0005 D3 `sensor_class` values
- [ ] Update `docs/sonar_ecosystem.md` to add a "Survey indexer / query" row referencing #258/#259 when the deliverable lands
- [ ] If a new ROS 2 package is created, ensure `package.xml` / naming / license headers follow ADR-0008

## Plan Authored
**Status**: complete
**When**: 2026-07-13 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-259/plan.md` at `19d48e6`
**Branch**: feature/issue-259 at `19d48e6`
**Phases**: single

### Open questions
- [ ] GGGS level default — issue says "store's native level" (~level 13, 1 m) but O(10^5) tile rows per survey pass may be impractical; propose level 11 (~4 m) as default with `--level` override. Needs user input before implementation.
- [ ] Sidescan `sensor_type` split — `sidescan-port`/`sidescan-stbd` vs single `sidescan`; plan proposes split in DB, `--sensor sidescan` matches both in query.
- [ ] Merge gap tolerance default — 5.0 s placeholder; field calibration needed; exposed as `--merge-gap <s>`.

## Plan Review
**Status**: complete
**When**: 2026-07-13 17:57 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-259/plan.md` at `19d48e6`
**PR**: PR-less (`--issue 259`)
**Verdict**: approve-with-suggestions

Independent fresh-context review (plan authored by Claude Sonnet; not a self-review).
Technical claims verified against the tree: GGGS API supports both required directions
(`Level::gridIndex` point/box→indices, `GridIndex::{west,east,south,north}…` index→bbox);
the bounded-TF pattern (60 s cache, guard interval, `kMaxPending`) exists verbatim in both
cited reference impls (`marine_sidescan_mosaic/src/sidescan_mosaic_bag.cpp`,
`cube_bathymetry/.../import_bag_main.cpp`); message types (`SonarDetections`, `RawSonarImage`)
correct; `docs/sonar_ecosystem.md` Arc 1 table present. Approach and scope are sound.

### Findings
- [ ] (must-fix) Core correctness paths untested — footprint→GGGS-tile enumeration and query tile-join have no test; issue-review asked for this. Both testable without bag I/O. — `plan.md:89`
- [ ] (must-fix) Schema comment overstates ADR-0005 D3 vocab — `sidescan-port`/`sidescan-stbd` extend, not equal, D3 (`sidescan`/`mbes-backscatter`/`mbes-bathy`). — `plan.md:79`
- [ ] (suggestion) ADR table mixes workspace vs project ADR numbers unlabeled — in-repo ADR-0002/0008 differ from cited workspace ADRs. — `plan.md:123`
- [ ] (suggestion) New package not added to `.agents/README.md` Package Inventory. — `plan.md:95`
- [ ] (suggestion) Schema is a cross-stage contract but documented only in ephemeral `plan.md` — consider a durable `docs/`/ADR home for stages 2–5. — `plan.md:59`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-13 19:12 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-259 at `04dc9d7`
**Mode**: pre-push
**Depth**: Deep (reason: new C++ package, 2502 LOC, cross-layer GGGS/store + SQLite + rosbag2/TF)
**Must-fix**: 4 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — silent index-corruption path + query-output bug warrant a fix pass and re-read

Static analysis clean (ament_cpplint; cppcheck TEST-macro/unused-member warnings are false positives). Two Claude Adversarial lenses (A logic, B systemic) converged on one theme: the indexer write path assumes every SQLite call succeeds and no exception occurs. Governance and plan adherence clean; all 4 prior plan-review findings resolved.

### Findings
- [ ] (must-fix) Indexer write/ledger path ignores all sqlite3_step/prepare return codes → failed insert still COMMITs and marks bag fully-indexed (silent partial index; re-run skips as "unchanged") — `marine_survey_index/src/survey_index_bag_main.cpp:209,514-559`
- [ ] (must-fix) Per-bag loop has no try/catch and no ROLLBACK → a corrupt message (deserialize throws) or SQLite failure aborts the whole run, remaining bags lost, db not closed — `marine_survey_index/src/survey_index_bag_main.cpp:322`
- [ ] (must-fix) Query results not globally sorted across 200-tile chunks → CLI prints duplicate/out-of-order bag headers for boxes spanning >200 tiles — `marine_survey_index/src/query.cpp:89-137`
- [ ] (must-fix) Unguarded std::stoi on --level → uncaught exception crash (inconsistent with other args' toInt guard) — `marine_survey_index/src/survey_index_query_main.cpp:155`
- [ ] (suggestion) kMaxPending overflow flush bypasses the TF guard interval → oldest ping may be dropped as no-tf under backpressure (cross-pass confirmed) — `marine_survey_index/src/survey_index_bag_main.cpp:499-501`
- [ ] (suggestion) samples_per_beam==0 → negative slant range flips sidescan footprint to wrong side; skip like sample_rate<=0 — `marine_survey_index/src/survey_index_bag_main.cpp:491-495`
- [ ] (suggestion) fingerprint/scanForBags use throwing filesystem iteration; broken symlink/permission error aborts run — use error_code overloads — `marine_survey_index/src/survey_index_bag_main.cpp:162,183`
- [ ] (suggestion) std::string from possibly-null sqlite3_column_text (latent UB; columns NOT NULL today) — `marine_survey_index/src/query.cpp:124`
- [ ] (suggestion) Plan drift: MBES level default L10 vs plan's "~L13 store-native" — per-sensor split refinement, consistent in code+help+schema doc; noted, not a defect — `marine_survey_index/src/survey_index_bag_main.cpp:273`
