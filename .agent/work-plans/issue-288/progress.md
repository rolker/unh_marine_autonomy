---
issue: 288
---

# Issue #288 — Canonical home for geospatial support data under ~/data/world/

## Issue Review
**Status**: complete
**When**: 2026-08-20 12:46 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #288
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

The issue proposes amending ADR-0010 D3 to add a `datum/` subtree under `~/data/world/`, giving VDatum grids, geoid files, and user override polygons a canonical, updater-managed location. It also relocates the S-102 fetch cache under `world/charts/`, repoints consumers, adds a materialization step for user datum polygons (from project repos into `world/datum/user/`), and gates the `mru_transform` CMake download block deletion on field-host provisioning.

This matches the established umbrella-tracker pattern for ADR-0010 follow-on work (uma#86, uma#272); each work item will become its own sub-issue/PR.

### Scope Assessment

**Well-scoped?** Yes as a design/tracking issue — six concrete, bounded work items; each will land as its own sub-issue or PR. The issue correctly calls out its parent (#86, ADR-0010) and the decision it settles (design settled with Roland 2026-07-31).

**Right repo?** Yes — unh_marine_autonomy owns ADR-0010 and the world model definition. s57_tools and mru_transform work items are correctly placed in their respective repos (s57_tools#28 referenced, mru_transform CMake deletion is a separate downstream task).

**Dependencies?**
- Requires s57_tools#28 (extend updater scope) — may not yet exist as an issue; should be created before the ADR amendment lands.
- The `mru_transform` CMake block deletion (item 6) is gated on field-host (gabby/salmon) provisioning via the `world/` sync or a deployment provisioning step.
- ADR-0010 amendment (item 1) should land first or alongside item 2 to avoid divergence between the ADR text and the implemented tree.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | User polygon materialization from git preserves PR review for safety-relevant hand-authored files; design rationale fully documented. |
| Enforcement over documentation | Watch | The CMake block deletion gate ("gated on field-host provisioning") has no named enforcement mechanism — easy to defer indefinitely. The work plan should define what "provisioned" means concretely (a provisioning script output, a deploy-step assertion, or a CI check). |
| Capture decisions, not just implementations | OK | This issue is precisely recording a design amendment to ADR-0010; the 2026-07-31 design discussion is attributed. |
| A change includes its consequences | Watch | `docs/sonar_ecosystem.md` reframe is listed in ADR-0010's Consequences section but not in this issue's work items. The survey index path (`~/data/stores/survey_index.db` → `~/data/world/`) is another known migration not explicitly called out in item 4. |
| Only what's needed | OK | `world/` layout is well-bounded; evictable caches explicitly excluded; non-goals section is crisp. |
| Improve incrementally | OK | Umbrella-tracker pattern is appropriate; each item lands separately. CMake deletion is correctly deferred until safety precondition is met. |
| Safety First (project principle) | OK | CMake deletion gated on field-host readiness; user polygons stay in git for PR review; updater nav-liveness check (ADR-0010 D7) retained. |
| Standards Compliance (project principle) | OK | No ROS 2 convention changes; library extraction (D6) follows established patterns. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| unh_marine_autonomy ADR-0010 (D3 amendment) | Yes | The issue IS the amendment request; item 1 commits it. The amendment must clarify the user-polygon materialization model (authored in git, not updater-managed) alongside the tree layout. |
| workspace ADR-0001 (adopt ADRs) | Yes | A design decision is being recorded. Amending ADR-0010 in the same PR as the code it documents is the correct pattern. |
| workspace ADR-0003 (project-agnostic workspace) | No | All content stays in project repos. |
| workspace ADR-0013 (progress.md vocabulary) | No | No new skill or entry type introduced. |

### Consequences

From ADR-0010's own Consequences section — items not explicitly listed in the issue's work items:
- `docs/sonar_ecosystem.md` reframe (ADR-0010 Consequences paragraph) — not in issue work items. Should be a named sub-task or explicitly deferred.
- Survey index path migration (`~/data/stores/survey_index.db`) — not called out in item 4; should be verified when repointing consumers.

Recommendations:
- Item 6 (delete CMake block) needs a named concrete gate: e.g., "deployment provisioning script runs `projsync`/VDatum download before field build; verified via a deploy-step output or a CI readiness check on the target host." Without this, the deletion may silently break the next fresh field build.
- The S-102 cache relocation path (`world/charts/` vs `s100/` sibling) is deferred to the updater — acceptable, but the sub-issue for item 3 should capture that the path must be recorded in the edition registry once chosen.
- Confirm s57_tools#28 is filed (or will be filed as part of this work) before the ADR amendment merges, so the referenced issue exists and is cross-linked.

### Actions
- [ ] Define a concrete, verifiable gate for the `mru_transform` CMake block deletion (item 6) — e.g., a provisioning script, deploy-step assertion, or field-host CI check — so "gated on field-host provisioning" cannot silently slip.
- [ ] Add `docs/sonar_ecosystem.md` reframe to the work item list (or explicitly defer it with a reason); it is listed in ADR-0010's own Consequences and is omitted here.
- [ ] Verify `~/data/stores/survey_index.db` path migration is captured in item 4 (repoint consumers).
- [ ] Confirm s57_tools#28 is filed before or alongside item 1 (ADR-0010 amendment).
