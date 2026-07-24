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
