---
issue: 208
---

# Issue #208 — Add along-track footprint splat to marine_sidescan_mosaic

## Issue Review
**Status**: complete
**When**: 2026-06-22 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #208
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

**Well-scoped?** Yes — Stage 3 of #185 with a tight scope: modify the projection/accumulator path in `marine_sidescan_mosaic` to compute an along-track footprint per sample (`tx_beamwidths[0] × slant_range`) and splat across the covered cells. Stage boundaries are clear (Stage 4 = radiometric corrections, explicitly out of scope).

**Right repo?** Yes — change is entirely within `unh_marine_autonomy/marine_sidescan_mosaic`, a project package. No workspace infra is touched.

**Dependencies?** Stage 2 (#200) must be merged for `depression_rad` to be available per sample. The issue assumes #200 is complete. No other open dependencies identified.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Configurable fallback beamwidth when `tx_beamwidths` is empty is the right design; keeps behavior visible and tuneable |
| Only what's needed | OK | Issue explicitly scopes to a bounded cell loop / segment rasterization — no Gaussian kernel, no speculative features |
| Improve incrementally | OK | Stage 3 of a staged roadmap; radiometric corrections deferred to Stage 4 |
| Test what breaks | OK | Acceptance criteria requires tests for footprint-length math and splat cell coverage, and no regression in Stage-2 projection tests |
| A change includes its consequences | Watch | Issue doesn't mention updating README/docs for the new fallback parameter; plan-task should ensure the ROS parameter is documented alongside the implementation |
| Modularity and Decoupling | Watch | The current accumulator API is `add(cell, value)` — a single cell per call. Splatting requires either the caller loops `add()` for each footprint cell, or a new `addSegment()` API is introduced. The issue says "per-sample cell loop" which implies caller-side looping, but the plan should make this explicit to avoid an unexpected API change |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0002 — Worktree isolation | Yes | Standard: work in this worktree, not a branch switch |
| ADR-0008 — Follow ROS 2 conventions | Yes | `tx_beamwidths[0]` convention settled in marine_tools#62; plan should verify the field name is accessible from the ping_info message available in this package's message types |
| ADR-0001 — Adopt ADRs | Watch | If the accumulator API is extended (e.g. `addSegment()`), that design choice merits a note in the commit or progress.md; a full ADR is not required for a package-internal API change |

### Consequences

- If a new ROS parameter (fallback beamwidth) is added: update launch file defaults and `README.md` in the same PR.
- If `projection.hpp` gains a new function (e.g. footprint-length helper): update `test_projection.cpp` to cover it.
- No `.msg`/`.srv` interface changes implied (reads existing `ping_info.tx_beamwidths` field).

### Recommendations

- Clarify slant range derivation in the plan: the issue gives `footprint ≈ slant_range × tx_beamwidth` but doesn't specify how slant range is computed. The existing `depression_rad` from Stage 2 plus vessel altitude (nadir depth) gives `slant_range = altitude / sin(depression_rad)` — this should be explicit in the plan to avoid ambiguity during implementation.
- Keep footprint computation in the projection/tier1 layer (caller side), passing a `float footprint_m` alongside `value` to the splatting loop rather than extending the `MosaicAccumulator` API — keeps the accumulator's interface clean and the footprint math separately testable.

### Actions
- [ ] Document the new fallback-beamwidth ROS parameter in README and the launch file within the same PR.
- [ ] Clarify slant range derivation in the plan (see Recommendations above).

## Plan Authored
**Status**: complete
**When**: 2026-06-22 06:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-208/plan.md` at `fbe8e96`
**Branch**: feature/issue-208 at `fbe8e96`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-22 05:21 +00:00
**By**: Claude Code Agent (Claude Opus)

<!-- Independence: the most-recent `## Plan Authored` By field is "Claude Code Agent
(Claude Sonnet)". The skill's name-only self-review heuristic would match (AGENT_NAME
is workspace-constant), but this is a genuinely independent review — fresh-context
sub-agent, different model (Opus vs the Sonnet author), separate dispatch — so no
self-review annotation is applied. -->

**Plan**: `.agent/work-plans/issue-208/plan.md` at `fbe8e96`
**PR**: PR-less (local plan; gh offline in this worktree)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Tier-1 v1→v2 format change adds `tx_beamwidth_rad` but `test/test_tier1.cpp` is omitted from Files to Change — no round-trip coverage for the new field, and `RejectsBogusSampleCount` assumes the sample-count uint32 is the last 4 bytes (`bytes.size()-4`); serialize the new field BEFORE the sample-count and update the test (or update that assumption) — `plan.md:26`, `plan.md:69`, `plan.md:79`
- [ ] (must-fix) Slant-range derivation is self-contradictory: Context defines `slant_range = altitude / sin(depression_rad)` (per-ping beam-center) but Approach step 4 uses the per-sample `slant` from `slantRange()` already in `onPing`. The per-sample value is correct (footprint scales with each sample's own range); remove/correct the Context formula — resolves review-issue Action #2 — `plan.md:20`, `plan.md:42`
- [ ] (suggestion) Centering loop `k ∈ [-(n_steps-1)/2, +(n_steps-1)/2]` with integer division under-covers by one cell for even `n_steps` (e.g. `n_steps=2` → only `k=0`); acceptable for gap-fill but note or center symmetrically — `plan.md:43`
- [ ] (suggestion) `sidescan_tier2_flat.cpp` and `sidescan_tier2_processed.cpp` get the identical splat loop ("same as tier2_flat"); consider a shared helper to keep them from diverging — `plan.md:48`, `plan.md:52`
