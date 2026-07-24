---
issue: 275
---

# Issue #275 — marine_bathymetry_store: chart source layer and wholesale regeneration

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 22:06 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-275 at `8fdd7e7`
**Mode**: pre-push
**Depth**: Deep (reason: navigation-safety data store + filesystem failure-recovery swap)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — one must-fix; the atomic swap's restore-on-failure safety guarantee is untested (mechanical to add). Build clean, all tests pass, cpplint clean.

### Findings
- [x] (must-fix) `replaceChartLayer` restore branch (rename backup→chart after a failed commit) is untested — the "failure leaves chart/ intact" guarantee is only verified for pre-validation refusals; force a mid-swap rename failure (e.g. read-only store dir) and assert restore — `src/tile_io.cpp:496-512` / `test/test_tile_io.cpp`
- [x] (suggestion) No mechanical gate stops a loaded Chart layer driving nav before the #272 cost-model rework (load bypasses the write-gate); out of scope but cross-reference #272 in the PR body and confirm no deployed store carries `chart/` yet — `src/query.cpp:93,111`
- [x] (suggestion) `warnIfUnrecognizedStoreLayout` doc comment + cerr message still list `chart/` as obsolete taxonomy and omit it from recognized layers — stale now that chart is real — `src/tile_io.cpp:156-165,193-198`
- [ ] (suggestion) staged-dir `.tif` check lacks an `is_regular_file()` guard (load path at :343 has one); a dir named `foo.tif` would pass — `src/tile_io.cpp:485`
- [ ] (suggestion) crash-recovery incomplete: an orphaned `.chart_backup/` from a mid-swap crash is discarded by the next run rather than restored to `chart/` — `src/tile_io.cpp:496-516`
- [ ] (suggestion) failure-path hardening (minor): detect EXDEV up front before renaming the live layer away; document/test the double-fault case where the restore rename itself throws (original exception lost) — `src/tile_io.cpp:500-512`

## Plan Review
**Status**: complete
**When**: 2026-07-24 21:42 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-275/plan.md` at `3c67e62`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

### Findings
- [x] (suggestion) `source_layer_count` is derived from `source_layers_by_priority.size()`, not a separate literal — the concrete edits are the enum `Chart = 2`, adding `Chart` to the initializer, **and** bumping the array arity `std::array<SourceLayer, 2>` → `<..., 3>`. A 3-element initializer into a `,2>` array won't compile; reword step 1 / Files table so the implementer changes the template arity. — `plan.md:17`, `plan.md:48`
- [x] (suggestion) `replaceChartLayer` robustness: a stale non-empty `.chart_backup/` left by a crashed prior run makes the `chart/`→`.chart_backup/` rename fail with `ENOTEMPTY`, breaking the next regeneration. Remove any pre-existing `.chart_backup/` before the swap. — `plan.md:28`
- [x] (suggestion) review-issue action #3 (cross-reference the cost-model rework issue #272 in the PR description) is not reflected in the plan — carry it into the PR body so the safety precondition is discoverable without re-reading ADR-0010 D7. — `plan.md:62`
- [ ] (note) The two-step backup-then-rename swap has a brief window where `chart/` is absent; acceptable under D7's enforced nav-down precondition (regeneration only while navigation is down). No action at this scope. — `plan.md:26`

## Plan Authored
**Status**: complete
**When**: 2026-07-24 17:45 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-275/plan.md` at `3c67e62`
**Branch**: feature/issue-275 at `3c67e62`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Issue Review
**Status**: complete
**When**: 2026-07-24 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #275
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First (project) | Watch | Issue correctly documents the safety precondition (cost-model rework required before chart data can drive navigation), but there is no mechanical enforcement preventing chart imports on a production system before that rework lands. Acceptable at this scope (store only); the precondition issue must be explicitly cross-referenced and tracked |
| Human control and transparency | OK | Write gate, atomic swap, and nav-liveness check (from ADR-0010 D7, partly in updater scope) all make behavior explicit and operator-visible |
| Enforcement over documentation | Watch | Write-gate refusal is a tested acceptance criterion (good). The safety precondition ("do not deploy chart data without cost-model rework") has no mechanical enforcement — relies on operator knowledge. Honest in the issue body; should be tracked as a follow-up gate |
| Capture decisions | OK | ADR-0010 D3/D7 is the decision record. Issue correctly references the ADR and does not re-litigate it |
| A change includes its consequences | Watch | `source_layers_by_priority` in `bathy_cell.hpp` currently enumerates only `{Survey, Reference}` — it must be extended with `Chart`. Any exhaustive switch/match on `SourceLayer` values requires a new `Chart` case. The claim that `shallowestReliable` "needs no change beyond walking the new layer" should be verified in code during implementation, not assumed |
| Only what's needed | OK | D8 (draft/processed re-split) is explicitly deferred. Issue scope is minimal and focused |
| Improve incrementally | OK | Single focused store-side change; updater/exporter are separate issues |
| Test what breaks | OK | Acceptance criteria cover the key scenarios: swap atomicity on simulated failure, write-gate refusal, prior-class ordering, and stale-tile removal on shrinking coverage |
| Modularity and decoupling (project) | OK | Adding a new layer type without touching other layers; `chart/` loader path mirrors the existing layer-dir pattern |
| Standards compliance (project) | OK | No departure from ROS 2 or existing package conventions expected |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 (Bathymetric data store) | Yes | Directly amended: `source_layers_by_priority`, `layerMap()`, `importTiles`/`set` write gates, and loader directory logic all need updating. Issue is aware of the gate pattern (mirrors `reference_writable`) |
| ADR-0010 D3 | Yes | Issue implements exactly this: `SourceLayer::Chart`, on-disk `chart/`, write-gate-only access |
| ADR-0010 D4 | Yes | Placeholder ordering `processed > draft > reference > chart` — until D8 lands the interim ordering is `survey > reference > chart`; `Chart` must be last in `source_layers_by_priority` |
| ADR-0010 D7 | Yes | Staged directory + atomic swap semantics align with the ADR. Edition registry must be written inside the staged layer directory (inside the swap, not before) |
| ADR-0008 (ROS 2 conventions) | Yes | Package changes; standard conventions should be followed |
| ADR-0013 (progress.md vocabulary) | Yes | This entry |

### Consequences

- `source_layers_by_priority` array in `bathy_cell.hpp` must include `Chart` (currently `{Survey, Reference}` only); all callers that iterate this array get the new layer automatically if they use the array, but any exhaustive switch/enum-check on `SourceLayer` needs a new case
- Tile I/O (`tile_io.cpp`) must map `SourceLayer::Chart` → `"chart/"` directory alongside existing `"survey/"` and `"reference/"` mappings
- `replaceLayer` (or equivalent) API is new public surface area — design should guard against accidental non-regeneration calls; consider a distinct staging type rather than a plain path argument to make the write gate harder to bypass
- The D8 migration ("existing `survey/` re-classifies wholesale to `processed/`") is a follow-on that co-lands with `cube_bathymetry` write-path changes; nothing in this PR handles it, but implementation should avoid introducing assumptions that break when D8 lands
- `bathymetry_layer` cost-model rework (#272 / ADR-0010 D7 precondition) must be tracked explicitly after this PR merges — the code paths exist but deploying actual chart data on the boat is blocked on that work

### Actions
- [ ] Verify `shallowestReliable` and all exhaustive `SourceLayer` enumerations in code (switch/match, constexpr arrays) before closing the PR — claim that "no change beyond walking the new layer" holds must be checked, not assumed
- [ ] Ensure `replaceLayer` API design guards against accidental non-regeneration-path writes; document the caller contract at the API boundary
- [ ] Cross-reference the cost-model rework issue in the PR description so reviewers and future agents can find the safety precondition without re-reading ADR-0010 D7
- [ ] Confirm the D8 non-interference guarantee: implementation must not introduce assumptions (e.g., hardcoded two-layer count) that break when `draft`/`processed` are added
