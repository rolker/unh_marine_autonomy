---
issue: 275
---

# Issue #275 — marine_bathymetry_store: chart source layer and wholesale regeneration

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
