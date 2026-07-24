---
issue: 274
---

# Issue #274 — New package: marine_vertical_datum (ADR-0010 D6)

## Issue Review
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #274
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

- **Well-scoped?** Yes. A focused library-extraction task with clear boundaries: new
  ROS-free package, defined API (`(lat, lon) → {mllw_z, mhhw_z?, source}`), existing
  source to port (`datum_config.{hpp,cpp}` from `mru_transform/`), and explicit
  non-goals (no mru_transform consumer migration, no S57 parsing). Single PR sized.
- **Right repo?** Yes. `unh_marine_autonomy` (core_ws) is correct per ADR-0010 D6's
  placement constraint: `s57_tools` (core), CAMP (ui), and `mru_transform` (platforms)
  must all be able to depend on a core_ws library.
- **Dependencies?** The source (`datum_config.{hpp,cpp}`) already exists in
  `mru_transform`. No blocking upstream issues; the `mru_transform` consumer migration
  is explicitly deferred as a non-goal. Issue #86 (umbrella) is the parent — this is
  one leaf on it.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Clear rationale, explicit non-goals, ADR-0010 D6 is the anchor |
| Capture decisions, not just implementations | OK | ADR-0010 D6 already recorded the design decision; issue implements it |
| A change includes its consequences | Watch | Temporary code duplication (datum_config stays in mru_transform until follow-on); issue acknowledges this explicitly — acceptable as a stated non-goal |
| Only what's needed | OK | Minimal scope, non-goals listed, no ROS runtime deps required |
| Improve incrementally | OK | Small extraction with clear before/after; follow-on issues handle consumer migration |
| Test what breaks | Watch | VDatum query mock seam is mentioned but not yet designed; implementation plan should specify how the mock boundary is expressed (e.g. injectable function pointer or abstract class) so CI grids-absent testing is actually achievable |
| Workspace vs. project separation | OK | Correctly placed in project repo, not workspace |
| Modularity and Decoupling (project) | OK | Core purpose of this issue |
| Standards Compliance (project) | OK | ROS-free library; package.xml/CMakeLists.txt must still follow REP-2000 / ROS 2 conventions (ADR-0008) |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0010 (Geospatial World Model, project) | Yes — directly implements D6 | Issue is well-aligned; acceptance criteria match D6's stated outputs |
| ADR-0008 (Follow ROS 2 Official Conventions) | Yes — new package | Must have valid package.xml/CMakeLists.txt; no rclcpp dep is fine for a pure library package |
| ADR-0001 (Adopt ADRs) | Not triggered | Decision already captured in ADR-0010 D6 |
| ADR-0002 (Worktree isolation) | Not triggered | Process concern, already in worktree |

### Consequences

- **Namespace migration**: `datum_config.hpp` currently uses `namespace mru_transform`.
  The new library will need its own namespace (e.g. `marine_vertical_datum`). The
  implementation plan should call this out — it affects the public API shape.
- **PROJ dependency**: `package.xml` must declare the PROJ dependency (e.g.
  `<depend>libproj-dev</depend>` or equivalent rosdep key). CMakeLists.txt needs
  `find_package(PROJ REQUIRED)`. Neither is mentioned in the issue — worth confirming
  in the plan.
- **README for grid provisioning**: Acceptance criteria require it; the plan should
  treat it as a deliverable alongside the library code (per "a change includes its
  consequences").
- When `mru_transform` later consumes the library (follow-on), `datum_config.{hpp,cpp}`
  there can be removed — that PR should update the dependency list and delete the
  duplicates.

### Recommendations

- Pin the mock-seam design before implementation: one injectable callable (e.g.
  `std::function<std::optional<VDatumResult>(double lat, double lon)>`) is the minimal
  seam that keeps tests grids-free on CI while leaving real PROJ wired up in production.
- Consider whether `DatumSource`, `LatLon`, `DatumEntry`, `VDatumResult`, and
  `DatumResult` structs should stay in a `marine_vertical_datum` namespace from day one,
  since downstream consumers will spell that namespace and the mru_transform namespace
  is a leaky implementation detail.

### Actions
- [ ] Specify the VDatum mock seam design in the implementation plan (injectable callable or abstract class) so CI tests are verifiable without grids.
- [ ] Confirm PROJ dependency declaration in package.xml / CMakeLists.txt is in scope.
- [ ] README for grid provisioning is a required deliverable — track it alongside library code, not as a post-merge follow-up.

## Plan Authored
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-274/plan.md` at `f5f2e6b`
**Branch**: feature/issue-274 at `f5f2e6b`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-24 18:51 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-274/plan.md` at `f5f2e6b`
**PR**: PR-less (`--issue` mode)
**Verdict**: approve-with-suggestions

Independent review (fresh-context #490 sub-agent, Opus; plan authored by the
Sonnet invocation) — not an author self-review despite the shared commit
identity. All findings are suggestions; none block implementation. They were
surfaced by reading the ported source (`chart_datum_node.cpp`,
`datum_config.hpp`) against the plan. All three review-issue action items
(mock seam, PROJ dep, README) are addressed.

### Findings
- [ ] (suggestion) Pin the error/diagnostic seam for the extracted PROJ functions — they use `RCLCPP_ERROR`/`RCLCPP_WARN_THROTTLE` (`chart_datum_node.cpp:253,359`) which a ROS-free lib can't call; decide exceptions vs return-status vs dropping warnings — `plan.md:42`
- [ ] (suggestion) Make `make_vdatum_query` the primary production entry; standalone `query_vdatum(lat,lon,VDatumConfig)` rebuilds the PROJ pipeline per call (footgun for per-cell S57 exporter) — `plan.md:45`
- [ ] (suggestion) Rename the include guard `MRU_TRANSFORM__DATUM_CONFIG_HPP_` → `MARINE_VERTICAL_DATUM__DATUM_CONFIG_HPP_` alongside the namespace — `plan.md:36`
- [ ] (suggestion) Use rosdep key `proj` + `pkg_check_modules(PROJ REQUIRED IMPORTED_TARGET proj)` / link `PkgConfig::PROJ` per mru_transform precedent (not `libproj-dev`) — `plan.md:83`
- [ ] (suggestion) Preserve `proj_context_set_enable_network(ctx, false)` (`chart_datum_node.cpp:286`) in the library per ADR-0010 D6/D7 offline guarantee — `plan.md:36`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 19:11 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-274 at `7640003`
**Mode**: pre-push
**Depth**: Deep (reason: 11 files & 1368 additions exceed 10-file/200-line thresholds; new cross-layer library per ADR-0010 D6)
**Must-fix**: 0 | **Suggestions**: 6
**Round**: 1 | **Ship**: recommended — no must-fix; faithful extraction (datum_config byte-identical to battle-tested source), only new-consumer suggestions remain

### Findings
- [x] (suggestion) Strengthen thread-safety note: shared `PJ` hazard (not just context) + "one make_vdatum_query per thread" for the parallel per-cell exporter — `marine_vertical_datum/include/marine_vertical_datum/vdatum_query.hpp:60`
- [x] (suggestion) Sort `collect_grids` output — unspecified `recursive_directory_iterator` order makes `+grids=` precedence non-deterministic across machines — `marine_vertical_datum/src/vdatum_query.cpp:32`
- [ ] (suggestion) Coverage gap: real `proj_trans` sign (`-z`), HUGE_VAL/isinf/isnan no-coverage detection, and `proj_torad` ordering are never executed (stubs + setup-failure only); add a gated synthetic-grid integration test — `marine_vertical_datum/test/test_vdatum_query.cpp`
- [ ] (suggestion) Consider resetting/checking `proj_errno` per call in `query_datum` for the millions-of-cells loop (low-confidence, inherited from production node) — `marine_vertical_datum/src/vdatum_query.cpp:95`
- [x] (suggestion) Stale ported comment "acceptance item 5 of issue #25" — should reference #274 — `marine_vertical_datum/test/test_datum_config.cpp:3`
- [ ] (suggestion) Convention drift: no `ament_lint_auto`/`ament_lint_common`, all 6 C++ files lack copyright headers (cpplint 6×legal/copyright, uncrustify 2 files) — calibrated down: sibling mru_transform's lint is a no-op and ships headerless too, so not a blocker — `marine_vertical_datum/CMakeLists.txt`

## Integrated Review
**Status**: complete
**When**: 2026-07-24 16:40 -0400
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #279 at `a3c0b7d`
**Sources**: 2 (Copilot R1 @ `a3c0b7d`, Local Review (Pre-Push) @ `7640003`)
**Cross-source confirmations**: 0
**CI**: all-pass (build 9m15s)

### Findings
- [ ] (valid, Copilot — mechanism corrected) unchecked `proj_context_create()` failure: NOT a null-deref crash (PROJ treats NULL ctx as the default context), but alloc failure would silently fall back to the process-global default context — un-owned by the RAII holder, networking flag applied globally; add a null check → diag + empty return — `marine_vertical_datum/src/vdatum_query.cpp:158`
- [ ] (valid, Copilot) fixed temp-dir name `mvd_test_empty_grid_dir` can collide across concurrent multi-worktree test runs on one host; use a unique (mkdtemp) directory — `marine_vertical_datum/test/test_vdatum_query.cpp:110`
- [ ] (valid, Copilot) `mkstemp` used with only `<unistd.h>`/`<cstdio>` — compiles via transitive gtest includes on glibc only; add `<cstdlib>` (latent in mru_transform's copy too; rides the follow-on migration there) — `marine_vertical_datum/test/test_datum_config.cpp:53`
- [ ] (valid-by-convention, Copilot) install path `include/${PROJECT_NAME}` + matching INSTALL_INTERFACE is internally consistent (no breakage) but 2 of 3 sibling store packages use plain `include` with explicit comments rejecting the doubled path; align with the sibling convention — `marine_vertical_datum/CMakeLists.txt:32`

### False positives
- (none — Copilot's C1 *mechanism* ("dereference null and crash") is wrong per PROJ's NULL-ctx-means-default convention, but the underlying unchecked-allocation concern is real, so it is classified valid with corrected rationale rather than dismissed)
