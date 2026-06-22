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

## Implementation
**Status**: complete
**When**: 2026-06-22 05:40 +00:00
**By**: Claude Code Agent (Claude Opus)

**Commit**: `7bf452b` on `feature/issue-208`
**Plan**: `.agent/work-plans/issue-208/plan.md` (synced this pass)

### What changed
Implemented the along-track footprint splat (Stage 3 of #185), folding in all four
review findings (operator-approved):

- **`projection.hpp`** — `GeoBeam` gains `heading_rad`; new inline `footprintAlongTrack()`
  and a **shared `splatAlongTrack()` template helper** (fold #4) used by `onPing` and both
  tier2 tools, so the loop has one implementation. The helper steps `n_steps =
  max(1, round(footprint/cell_size))` cells along `heading_rad` with **symmetric centering**
  `offset_k = (k − (n_steps−1)/2)·cell` (fold #3) so even footprints aren't under-covered;
  `n_steps==1` ⇒ offset 0 ⇒ the legacy point-deposit, bit-for-bit unchanged. The header now
  includes `geodesy/geodesics.h` for the along-track step.
- **`projection.cpp`** — populate `GeoBeam.heading_rad` (body +X yaw, column 0 of `r_body_ned`).
- **Per-sample slant range** (must-fix #2) — footprint uses the per-sample `slant` from
  `slantRange()` already in each loop, not a per-ping beam-center; the bad Context formula was
  removed from `plan.md`.
- **Tier-1 v1→v2** — `Tier1Ping.tx_beamwidth_rad`, serialized **before** the trailing
  sample-count uint32 (must-fix #1) so `readTier1Ping`'s corruption guard and the
  `RejectsBogusSampleCount` test (which assumes the count is the last 4 bytes) stay valid —
  verified: the count is still the final field. `kTier1Version` → 2.
- **`sidescan_mosaic_bag.cpp`** — populate `tx_beamwidth_rad` from `ping_info.tx_beamwidths[0]`.
- **`mosaic_node.cpp`** — `tx_beamwidth_fallback_rad` ROS param (default 0.0); splat via the
  shared helper.
- **`sidescan_tier2_flat.cpp` / `sidescan_tier2_processed.cpp`** — `--tx-beamwidth-fallback-rad`
  CLI arg + shared-helper splat.
- **Docs/launch** — README param row + pipeline step-5 note; launch `tx_beamwidth_fallback_rad`.
- **Tests** — `test_tier1` round-trips the new field (must-fix #1); `test_projection` adds
  `FootprintAlongTrack`, `SplatCoverageSpansFootprint` (incl. even-n_steps symmetry),
  `SplatStepsAlongHeading`, and `BeamHeadingMatchesGroundTrack`.

### Build & test
Built sibling deps first (geodesy in underlay; marine_autonomy/backscatter/
tiled_raster_store via `--packages-up-to`) — these were unbuilt in the worktree, not code
issues. `colcon build --packages-up-to marine_sidescan_mosaic` succeeded (only pre-existing
geodesy-header warnings). **gtest XML all green**: `test_projection` 14/14, `test_accumulator`
10/10, `test_normalizer` 5/5, `test_tier1` 5/5 — no regression in the Stage-2 projection tests.

The only `colcon test` failures are the **known local uncrustify 0.78.1 leading-vs-trailing
`?:` drift** and two **pre-existing** >100-char lines in `sidescan_mosaic_bag.cpp` (113/115
chars at HEAD, untouched by this change). My new ternaries match the committed leading-`?:`
style of the surrounding code (e.g. the adjacent `p.channel`/`p.sound_speed` ternaries), so
they pass a correct uncrustify and only trip the broken local 0.78.1; per the task guidance I
did not reformat base files to chase it. Verdict confirmed via gtest XML.

### Next step
Ready for `review-code`.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-22 05:51 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-208 at `a387093`
**Mode**: pre-push
**Depth**: Deep (reason: 14 files >10 and ~516 lines >200; binary-format change in a real-time safety pipeline)
**Must-fix**: 1 | **Suggestions**: 4
**Round**: 1 | **Ship**: continue — one genuine UB/robustness must-fix in the live `onPing` callback; address before push

### Findings
- [x] (must-fix) Validate/bound `tx_beamwidth_fallback_rad`: `static_cast<int>` of a NaN/inf `footprint_m` is UB, and a unit-confusion value (0.44 rad vs 0.00768) blows `n_steps` to ~117k deposits/ping with no clamp in the real-time callback — add `std::isfinite` guard + `n_steps` ceiling (cross-pass confirmed, Lens A+B) — `src/mosaic_node.cpp:88`, `include/marine_sidescan_mosaic/projection.hpp:178`
- [x] (suggestion) `round()` collapses 1.0–1.5-cell footprints to a single deposit, can still leave the gap; consider `ceil` — `include/marine_sidescan_mosaic/projection.hpp:178`
- [x] (suggestion) v1 `.sst1` files now report "not a Tier-1 stream"; distinguish version-mismatch from bad-magic — `src/sidescan_tier2_flat.cpp`, `src/sidescan_tier2_processed.cpp`
- [x] (suggestion) Splat tests assert only `size>=2`; pin symmetry (centroid≈origin) and along-heading (longitude≈const) — `test/test_projection.cpp:402`, `test/test_projection.cpp:410`
- [x] (suggestion) `onPing` has no try/catch around projection; `geodesy::wgs84::direct` can throw (largely pre-existing, low-probability) — `src/mosaic_node.cpp`

Static analysis clean (only known-broken local uncrustify 0.78.1 `?:` drift + pre-existing >100-char lines untouched by this PR). Plan adherence full; all prior review folds implemented. No `review-context.yaml` found (review used `.agents/README.md` + project PRINCIPLES/ADRs).

## Implementation
**Status**: complete
**When**: 2026-06-22 11:01 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-208 at `e394814`
**Addressed**: Local Review (Pre-Push) (2026-06-22 05:51 +00:00, branch feature/issue-208 at `a387093`) — its 1 must-fix + 4 suggestions, all actioned (none deferred).
**Commits**: `3bcf3b6`, `05d744a`, `11e49f2`, `e394814`

### Actions
- [x] (must-fix) Bound the along-track splat — `splatAlongTrack` now guards `footprint_m` with `std::isfinite` (a NaN/inf cast to `int` was UB) and hard-caps `n_steps` at `kMaxSplatSteps=1024`; `mosaic_node` validates `tx_beamwidth_fallback_rad` at startup (resets a non-finite/negative value to 0, warns on a >0.1 rad degrees-for-radians slip) — `include/marine_sidescan_mosaic/projection.hpp`, `src/mosaic_node.cpp` (`3bcf3b6`)
- [x] (suggestion) `round` → `ceil` for `n_steps` so a 1.0–1.5-cell footprint paints 2 cells instead of collapsing the gap; sub-cell footprints still yield 1 (legacy point-deposit preserved) — `include/marine_sidescan_mosaic/projection.hpp` (`3bcf3b6`)
- [x] (suggestion) Distinguish Tier-1 version-mismatch from bad-magic — new `checkTier1Header()` → `Tier1HeaderStatus {Ok, BadMagic, BadVersion}`; both tier2 tools print a distinct, actionable message on a version mismatch (reporting found vs expected version) — `include/marine_sidescan_mosaic/tier1.hpp`, `src/tier1.cpp`, `src/sidescan_tier2_flat.cpp`, `src/sidescan_tier2_processed.cpp` (`05d744a`)
- [x] (suggestion) Strengthen splat tests — `SplatCoverageSpansFootprint` now pins even-step symmetry (mean cell latitude within a cell of the origin); `SplatStepsAlongHeading` pins longitude ~constant (within a cell or two) while latitude spreads — `test/test_projection.cpp` (`11e49f2`)
- [x] (suggestion) Wrap the `onPing` per-sample projection loop in try/catch (`geodesy::wgs84::direct` can throw); drop the offending ping with a throttled warn rather than tearing down the executor — `src/mosaic_node.cpp` (`e394814`)

### Build & test
`./build.sh marine_sidescan_mosaic` succeeded (only pre-existing geodesy-header warnings). All gtest suites green on the rebuilt binaries: `test_projection` 14/14 (incl. the two new symmetry/along-heading assertions), `test_tier1` 5/5, `test_accumulator` 10/10, `test_normalizer` 5/5.

The only `colcon test` non-gtest failures are the **known/pre-existing** ones documented in the source review, untouched by this pass: 2 cpplint >100-char lines in `src/sidescan_mosaic_bag.cpp` (113/115 chars, not modified here) and the broken local uncrustify 0.78.1 leading-vs-trailing `?:` drift on the pre-existing `tx_beamwidth_rad` ternary (`mosaic_node.cpp` line ~348, not modified here). No new lint introduced by these fixes.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 208 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-22 11:16 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-208 at `ac66578`
**Mode**: pre-push
**Depth**: Deep (reason: Tier-1 binary-format change in a real-time safety pipeline; cross-file change)
**Must-fix**: 1 | **Suggestions**: 3
**Round**: 2 | **Ship**: recommended — one mechanical must-fix (cap-before-cast reorder), not rising from Round 1; apply it (and ideally suggestion #1) and push rather than another full round

### Findings
- [x] (must-fix) `static_cast<int>(ceil(footprint_m/cell_size))` runs before the `kMaxSplatSteps` clamp; a large-but-finite footprint (>INT_MAX cells, reachable via Tier-2 CLI `--tx-beamwidth-fallback-rad 1e30` or a corrupt per-ping beamwidth) is UB — the isfinite guard only catches NaN/inf. Clamp the ratio in `double` before the cast (cross-pass confirmed, Lens A + Lens B) — `include/marine_sidescan_mosaic/projection.hpp:190`
- [x] (suggestion) Per-ping `ping_info.tx_beamwidths[0]` (and the Tier-2 stored/CLI beamwidth) get only a `>0.0` guard — the finiteness/upper-bound/degrees-slip validation applied to `tx_beamwidth_fallback_rad_` is bypassed for the dominant input path; share it via a helper — `src/mosaic_node.cpp:347`, `src/sidescan_tier2_flat.cpp`, `src/sidescan_tier2_processed.cpp`
- [ ] (suggestion) Even-n_steps symmetry test uses a full-cell tolerance; a one-sided splat (centroid +0.5 cell) would still pass. Tighten to <0.5*cell_deg or test a larger even n_steps — `test/test_projection.cpp:670`
- [x] (suggestion) `onPing` try/catch wraps the whole per-sample loop, so one throwing sample drops the ping's already-deposited good samples; per-sample skip would preserve the rest of the swath (executor protection itself is correct) — `src/mosaic_node.cpp:368`

Static analysis clean for new code (only known pre-existing uncrustify 0.78.1 `?:` drift + untouched >100-char lines in `sidescan_mosaic_bag.cpp`; the lone >100-char added line is a Markdown table row, not linted). Plan adherence full; all Round-1 pre-push findings (1 must-fix + 4 suggestions) confirmed addressed. No `review-context.yaml` found; offline review diffed against local `origin/jazzy`.
