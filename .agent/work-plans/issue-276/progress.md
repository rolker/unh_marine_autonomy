---
issue: 276
---

# Issue #276 — bathymetry_layer: worst-case-clearance cost model (ADR-0010 D7 precondition)

## Issue Review
**Status**: complete
**When**: 2026-07-31 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #276
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

Single package (`bathymetry_layer`), well-defined change to cost model with explicit before/after semantics, four named test cases, and sim validation requirement noted. Achievable in one PR. Part of umbrella #86; is itself a hard precondition for chart ingestion (#275).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | New cost model rationale recorded in ADR-0010 D7; parameter migration documented in README per scope; configurable thresholds |
| Enforcement over documentation | Watch | Sim validation per ADR-0002 D7 is required before field use; the enforcement mechanism (who blocks deployment if sim hasn't passed?) is not specified in the issue — plan phase should nail this down |
| Capture decisions, not just implementations | OK | Design settled 2026-06-25, recorded in ADR-0010 D7; issue references the ADR |
| A change includes its consequences | Watch | Issue covers tests and README param migration. Does not explicitly address: (1) platform-repo nav2_params (bizzy/echoboat) that carry `max_uncertainty: 0.5` — the README already flags these as cross-repo follow-on but the parameter rename affects them directly; (2) whether `shallowestReliable` (in `marine_bathymetry_store`) needs a signature change to expose σ for caution costing — if so that is an inter-package dependency |
| Only what's needed | OK | Focused: one layer, rework one function pair, targeted tests |
| Improve incrementally | OK | Single bounded PR |
| Test what breaks | OK | Issue names the four critical regression cases correctly: keepout only on trusted-shallow, chart-σ shallow → caution not LETHAL, σ=∞/no-data unchanged, ramp continuity at gate boundaries |
| Workspace vs. project separation | OK | Change is entirely in the `unh_marine_autonomy` project repo |
| Safety First (project) | OK | Model is strictly safer for chart data: no false keepout of CATZOC regions while trusted shoal keepout is preserved; σ=∞/no-data conservatism unchanged |
| Simulation-First Validation (project) | Watch | ADR-0002 D7 cited but sim harness and "validated" criterion unspecified; plan phase should identify the scenario |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0002 — Worktree isolation | Yes | Worktree and branch `feature/issue-276` exist |
| Workspace ADR-0008 — ROS 2 conventions | Yes | Parameter rename must follow ROS 2 param conventions; issue mentions README migration but not deprecation warnings |
| Workspace ADR-0013 — progress.md vocabulary | Yes | This entry |
| Project ADR-0010 D7 — Geospatial World Model | Yes | This issue IS the D7 chart-ingestion precondition; the ADR specifies the cost model exactly |
| Project ADR-0002 — Bathymetric Data Store | Yes | `shallowestReliable` is the current query entry point; new semantics may require the store API to return σ alongside depth |

### Consequences

- **`shallowestReliable` API**: current signature collapses over-uncertain samples to `nullopt` — the caution ramp requires σ alongside depth. If `shallowestReliable` is not extended, `evaluateCell` cannot distinguish "trusted-shoal LETHAL" from "high-σ caution" without a separate query. This is the critical implementation fork to resolve in plan-task.
- **Platform-repo nav2_params**: `bizzyboat_project11` and `echoboat_project11` configs carry `max_uncertainty: 0.5`. Parameter rename/deprecation must propagate there; track as follow-on if not landing in the same PR.
- **`s57_layer` interaction**: no change needed — `bathymetry_layer` still max-cost combines; the new caution costs simply don't raise to LETHAL for chart-σ cells.

### Actions
- [ ] Plan-task: resolve the `shallowestReliable` API question — does the store query need to return σ, or does `evaluateCell` make two separate queries? Determine if `marine_bathymetry_store` is in scope.
- [ ] Plan-task: name the new parameters that replace `max_uncertainty` in its reject role (confidence gate threshold, caution cost scale) so the README migration section is concrete.
- [ ] Plan-task: identify the sim harness and define the "sim-validated" criterion per ADR-0002 D7 before field use.
- [ ] Follow-on (not blocking this PR): update platform-repo nav2_params when `max_uncertainty` is renamed/deprecated.

## Plan Authored
**Status**: complete
**When**: 2026-07-31 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-276/plan.md` at `2bc009c`
**Branch**: feature/issue-276 at `2bc009c`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-31 19:36 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent review. The `## Plan Authored` By-name ("Claude Code Agent")
collides with this reviewer's name (workspace convention: all Claude agents
share that name), but this is a fresh-context sub-agent on a different model
(Opus reviewing a Sonnet-authored plan), dispatched independently — not an
author self-review, so no self-review annotation. -->

**Plan**: `.agent/work-plans/issue-276/plan.md` at `2bc009c`
**PR**: PR-less (`--issue` mode; branch `feature/issue-276`)
**Verdict**: approve-with-suggestions

Core approach verified against source and ADR-0010 D7 and is sound. The
critical fork review-issue flagged — does `marine_bathymetry_store` need an
API change to expose σ? — is correctly RESOLVED: `shallowestReliable(*store_,
cell, ∞)` returns the shallowest finite-σ sample (nullopt only when every σ is
NaN → the existing "data but no reliable sample" LETHAL path), and
`DepthSample.uncertainty` already exposes σ for the `trusted` test. No store
change needed (verified `query.cpp:131`, `query.hpp:80`). ADR-0010 D7 semantics
(keepout only on trusted σ ≤ gate; high-σ → costed caution, never LETHAL alone)
match the ADR text. Scope is single-package / single-PR / ~4 files — correct.

### Findings
- [ ] (must-address) Sim-validation gate dropped — review-issue actioned plan-task to "identify the sim harness and define the 'sim-validated' criterion per ADR-0002 D7 before field use"; the plan is silent. Record the decision: define the scenario, or explicitly mark it a non-blocking pre-field gate with rationale, so the review-issue action isn't lost. — `plan.md:22` (Approach) / `plan.md:94` (Estimated Scope)
- [ ] (suggestion) README has a *second* `max_uncertainty` occurrence in the example nav2 config block (`bathymetry_layer/README.md:124`), not just the parameter table (`:70`). Plan step 5 names only the "parameter table" — update the example snippet too. — `plan.md:52`
- [ ] (suggestion) Silent config drift on rename — existing boat/platform configs setting `max_uncertainty:` will be silently ignored after the rename and fall back to `confidence_gate_` default 0.5, *and* the semantics change (reject-filter → trust-threshold). Per "Enforcement over documentation" (review-issue's ADR-0008 note flagged "not deprecation warnings"), consider a one-shot WARN if the deprecated key is still declared; at minimum the README migration note must call out the semantic change, not just the rename. — `plan.md:23`, `plan.md:87`
- [ ] (suggestion) Member rename ripples beyond `computeCost` callers — the test helper `setMaxUncertainty` (`test_bathymetry_layer.cpp:60`) and comment at `:232` also reference `max_uncertainty`. The test file is already in the plan's file list, so it's in scope; just call out that the rename touches these, not only test cases 1 & 8. — `plan.md:44`
- [ ] (suggestion, low) Worst-case selection — `evaluateCell` uses the σ of the *shallowest-depth* sample; the true worst-case clearance is min over samples of (clearance − σ), and a deeper-but-noisier sample could bind tighter. With one fused surface per layer (ADR-0002 #221) this is near-moot and matches the existing safety-query design, but worth a one-line confirmation against ADR-0010 D7 intent. — `plan.md:38`

## Implementation
**Status**: complete
**When**: 2026-07-31 20:30 +00:00
**By**: Claude Opus

**Plan**: `.agent/work-plans/issue-276/plan.md` at `45b728b` (amended from `2bc009c`)
**Branch**: feature/issue-276
**Commits**:
- `45b728b` plan: amend #276 with operator decisions + 5 plan-review findings
- `4e54071` feat(bathymetry_layer): worst-case-clearance cost model (ADR-0010 D7)
- `a6e46c0` docs(bathymetry_layer): document worst-case-clearance cost model + migration
- (this entry)

### What changed

Reworked `bathymetry_layer`'s cost model per ADR-0010 D7 (confirmed against
`docs/decisions/0010-geospatial-world-model.md` D7: "high-σ ⇒ go-slow, keepout
only on trusted data"):

- **`computeCost(double worst_case_clearance, bool trusted)`** — new signature.
  The ramp is trust-independent; only the below-`minimum_depth` verdict depends
  on trust: trusted → `LETHAL_OBSTACLE`, untrusted → `MAX_NON_OBSTACLE` (caution
  cap, never LETHAL on uncertainty alone). Cost is continuous across the gate.
- **`evaluateCell`** — calls `shallowestReliable(*store_, cell, ∞)` (no store API
  change, per the Plan Review), computes `worst_case = clearance − σ` and
  `trusted = (σ ≤ confidence_gate_)`, returns `computeCost(worst_case, trusted)`.
- **σ=∞ correctness detail (not in the original plan; found in implementation):**
  `shallowestReliable(∞)` returns nullopt only for all-NaN σ — it does **not**
  filter a literal σ=∞ sample (`∞ > ∞` is false; `query.cpp:131`). Since D7
  buckets σ=∞ (unknown quality, D4) with no-data rather than caution,
  `evaluateCell` guards `!sample || !std::isfinite(sample->uncertainty)` →
  conservative LETHAL. Verified this preserves "σ=∞/no-data conservative" while
  "large *finite* σ ⇒ caution".
- **`max_uncertainty` → `confidence_gate`** rename (param + member), with a
  one-shot deprecation `WARN` + `deprecated_max_uncertainty_seen_` latch when a
  config still declares the removed key (detected via a NaN-sentinel declare).
  Startup INFO log and class doc updated.

### Plan-Review findings — all five folded in (amended in the plan first)

1. (must-address) **Sim-validation gate** — recorded the operator decision:
   **non-blocking / advisory**. Unit tests are the enforced correctness bar; sim
   adds integration confidence but does NOT gate the August deployment. Written
   into the plan's new "Sim-Validation Decision" section; the review-issue action
   is resolved, not lost.
2. README example nav2 config block updated (`confidence_gate`), not just the
   parameter table.
3. One-shot deprecation WARN implemented + README migration note that calls out
   the **semantic** change (reject-filter → trust-threshold), not just the rename.
4. Test-file rename ripples covered: `setMaxUncertainty` → `setConfidenceGate`
   helper and the `:232` comment; additionally the two tests that pinned the OLD
   reject-filter (`OverUncertainSurveyedCellIsLethal`,
   `OverUncertainCellIsLethalWithNoPriorFallback`) were **reframed** to the new
   model — the plan's original step-4 list missed these; leaving them would have
   been a test failure.
5. (low) Worst-case selection confirmed against ADR-0010 D7 intent: with one
   fused surface per layer (#221) the shallowest-depth sample's σ is the single
   per-cell sample, so shallowest-σ and min-over-samples coincide. No behavior
   change; documented in an `evaluateCell` code comment.

### Tests

Four issue-named cases plus deprecation coverage (in
`test/test_bathymetry_layer.cpp`):
- `KeepoutOnlyOnTrustedShallow` — trusted worst-case-shallow → LETHAL; same depth
  untrusted → caution (keepout reserved for trusted data).
- `UntrustedShallowCellIsCautionNotLethal` (reframed test 4) — chart-σ shallow →
  `MAX_NON_OBSTACLE`, not LETHAL.
- `UnknownUncertaintyCellStaysConservativeLethal` (reframed test 7) — σ=∞ and
  σ=NaN → conservative LETHAL; confident control → FREE.
- `RampContinuityAcrossConfidenceGate` — ramp cost is trust-independent (no
  discontinuity as σ crosses the gate); the only split is the below-`minimum_depth`
  keepout for trusted data.
- `DeprecatedMaxUncertaintyKeyIsDetected` (+ negative control in the map-frame
  test) — the deprecation latch/WARN fires iff a config still sets the old key.
- Existing tests 1, 3, 6, 8 updated for the new `computeCost` signature /
  `setConfidenceGate` rename; existing no-data tests (2/2b/2c) unchanged.

### Build/test status

**Not run in this container — known limitation.** `bathymetry_layer` depends on
`marine_bathymetry_store`, which needs `marine_interfaces`,
`marine_vertical_datum`, `marine_autonomy`, `marine_tiled_raster_store` built;
none are compiled in this worktree and `underlay_ws/install` is empty. A
`./build.sh marine_bathymetry_store bathymetry_layer` attempt failed at
dependency resolution (missing `package.sh` for the four packages above), not on
this change's code. Manual verification done: line lengths < 100; nav2 cost
constants confirmed (`LETHAL_OBSTACLE=254`, `MAX_NON_OBSTACLE=252`,
`FREE_SPACE=0`) against the ramp math and every new assertion; no stale
`max_uncertainty_` / single-arg `computeCost` / `setMaxUncertainty` references
remain in source or tests. **The host runs the full build + test before push.**

### Next step

Host: run `core_ws` build + `bathymetry_layer` (and `marine_bathymetry_store`)
tests, then review-code. Follow-on (unchanged, not blocking): platform-repo
`nav2_params` `max_uncertainty` → `confidence_gate` migration; sim acceptance run
(now explicitly advisory).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-31 20:50 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-276 at `f24d7be`
**Mode**: pre-push
**Depth**: Deep (reason: safety-critical costmap cost model + cross-layer ADR-0010 precondition)
**Must-fix**: 3 | **Suggestions**: 2
**Round**: 1 | **Ship**: continue — a must-fix is a genuine safety regression (trusted-keepout masking), not mechanical; warrants another read after fixes.

Specialists: Static Analysis (ament_cpplint clean; cppcheck = pre-existing style only), Claude Adversarial x2 (Lens A logic, Lens B systemic), Governance, Plan Drift. Local Adversarial off (--no-local, workspace#590). Copilot off (default).

### Findings
- [x] (must-fix) Trusted-keepout REGRESSION on multi-sample cells: shallowestReliable(∞) retains over-gate samples and selects shallowest point-estimate; a shallower untrusted sample masks a co-located trusted LETHAL (capped at caution). Reachable via `source_layers_by_priority = {Survey, Reference, Chart}`. Cost by MAX over all reliable samples, not one shallowest-depth pick; add multi-layer test; fix "the two coincide" comment — `bathymetry_layer/src/bathymetry_layer.cpp:895-923`, `marine_bathymetry_store/src/query.cpp:134`
- [x] (must-fix) sigma=inf => LETHAL contradicts D7 "CATZOC D/U never keepout-grade" (D4: "unknown => sigma=inf"); finite-sigma-for-D/U contract is unbound for the future exporter. Add contract note to ADR-0010 D7 (~line 230) + update bestSource precondition note — `docs/decisions/0010-geospatial-world-model.md:230`, `marine_bathymetry_store/src/query.cpp:98`
- [x] (must-fix) `confidence_gate` unvalidated => fail-quiet: NaN/negative/zero silently disables ALL keepout (sigma <= NaN always false). Validate finite & >=0 with reset-to-default, mirroring the depth-ramp guard at :75-85 — `bathymetry_layer/src/bathymetry_layer.cpp:87`
- [x] (suggestion) computeCost hpp doc: note the sigma=inf/NaN => LETHAL path lives outside the trust gate (in evaluateCell), so the trust logic isn't the whole story — `bathymetry_layer/src/bathymetry_layer.hpp:89`
- [x] (suggestion) Stale S4 comment references non-existent "parameter-change callbacks" (no add_on_set_parameters_callback in package); reachable non-onInitialize path is the test setters — `bathymetry_layer/src/bathymetry_layer.cpp:835`

## Implementation
**Status**: complete
**When**: 2026-07-31 21:09 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-276 at `9169f23`
**Addressed**: `## Local Review (Pre-Push)` (2026-07-31 20:50 +0000, branch `feature/issue-276` at `f24d7be`; changes-requested, 3 must-fix + 2 suggestions)
**Commits**: `bc5d81e`, `027b372`, `99f2ecc`, `3989c71`, `9169f23`

Addressed all five findings from the pre-push review — one commit each, atomic.

### Actions
- [x] (must-fix) Trusted-keepout regression on multi-sample cells — added `reliableSamples()` to the store query (returns EVERY reliable sample, not just the shallowest) and reworked `evaluateCell` to cost by the **MAX** over all reliable samples, so a shallower untrusted sample can no longer mask a co-located trusted LETHAL. σ=∞ still short-circuits to conservative LETHAL. Fixed the stale "the two coincide" comment; added the `TrustedKeepoutNotMaskedByShallowerUntrustedSample` regression test (Survey trusted-keepout + shallower untrusted Chart sample ⇒ LETHAL). — `marine_bathymetry_store/src/query.cpp` (`reliableSamples`), `.../query.hpp`, `bathymetry_layer/src/bathymetry_layer.cpp` (`evaluateCell`), test — `bc5d81e`
- [x] (must-fix) σ=∞ vs "CATZOC D/U never keepout-grade" — documentation: recorded the finite-σ export contract in ADR-0010 D7 (D/U must export a large **finite** σ; σ=∞ stays reserved for genuinely-unknown D4 quality → conservative LETHAL) and mirrored the contract into the `bestSource` precondition note. No behaviour change — the consumer policy is correct; the ADR now binds the future S57 exporter. — `docs/decisions/0010-geospatial-world-model.md`, `marine_bathymetry_store/src/query.cpp` — `027b372`
- [x] (must-fix) `confidence_gate` unvalidated fail-quiet — added a finite-&-strictly-positive guard in `onInitialize` (mirrors the depth-ramp guard) that resets a NaN/negative/zero gate to the default with a WARN, so an invalid gate can no longer silently disable ALL trusted keepout. Added the `InvalidConfidenceGateResetsToDefault` test (negative/zero/NaN reset + valid pass-through). — `bathymetry_layer/src/bathymetry_layer.cpp`, test — `99f2ecc`
- [x] (suggestion) `computeCost` hpp doc — noted that the σ=∞/NaN ⇒ LETHAL bucketing lives in `evaluateCell` upstream of `computeCost`, so the trust flag here is not the whole safety story. — `bathymetry_layer/src/bathymetry_layer.hpp` — `3989c71`
- [x] (suggestion) Stale S4 comment — replaced the non-existent "parameter-change callbacks" reference with the real bypass path (the protected test setters); noted there is no `add_on_set_parameters_callback` in this layer. — `bathymetry_layer/src/bathymetry_layer.cpp` — `9169f23`

### Build/test status

**Full build + test PASS in-container this round.** Built the missing `geodesy`
underlay dependency from source (`underlay_ws/src/geographic_info/geodesy`), which
unblocked `bathymetry_layer` — resolving the "not run in this container"
limitation from the prior implementation entry. Results:
- `marine_bathymetry_store`: `colcon build` + `colcon test` — 14/14 pass (incl. copyright/cpplint/cppcheck/uncrustify/lint_cmake/xmllint).
- `bathymetry_layer`: `colcon build` + `colcon test` — 7/7 test executables, **24/24 gtest cases** + plugin-load + all linters pass. The two new tests
  (`TrustedKeepoutNotMaskedByShallowerUntrustedSample`, `InvalidConfidenceGateResetsToDefault`) pass, and every pre-existing test still passes.

All five findings' files pass `ament_cpplint` and `ament_uncrustify` clean.

### Next step

Lifecycle: **Implementation → review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 276 --skill review-code

Follow-on (unchanged, not blocking this PR): platform-repo `nav2_params`
`max_uncertainty` → `confidence_gate` migration; sim acceptance run (advisory).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-31 21:19 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-276 at `dfe5b59` (code HEAD `9169f23`)
**Mode**: pre-push
**Depth**: Deep (reason: safety-critical costmap cost model + cross-layer shared-lib API + ADR-0010 change)
**Must-fix**: 1 | **Suggestions**: 3
**Round**: 2 | **Ship**: recommended — sole must-fix is a one-line, mechanical README algorithm-prose fix (no design question); apply it (± suggestions) and push rather than run another full round.

Specialists: Static Analysis (ament_cpplint clean, ament_uncrustify clean, cppcheck = pre-existing style noise only), Claude Adversarial x2 (Lens A logic + Lens B systemic — both no must-fix), Governance, Plan Drift. Local Adversarial off (--no-local, workspace#590). Copilot off (default).

All three R1 must-fixes verified correctly resolved: MAX-over-reliable-samples preserves the trusted-keepout guarantee in every layer-priority permutation (max is order-independent; early-break at LETHAL sound — computeCost never emits NO_INFORMATION=255); σ=∞ short-circuit correct; confidence_gate fail-quiet guard correct; max_uncertainty deprecation fails safe (NaN sentinel → ignored + warned).

### Findings
- [x] (must-fix) README algorithm prose stale: item 3 still names `shallowestReliable(store, cell, ∞)` "returns the shallowest sample" as the live model — contradicts README:77 (worst-case/MAX), the code (`reliableSamples` + MAX-over-samples), and re-describes the exact superseded shallowest-pick behavior that was R1's trusted-keepout safety regression — `bathymetry_layer/README.md:67`
- [ ] (suggestion) New public shared-lib API `reliableSamples()` has no direct unit test (only transitive via layer regression test); add a case pinning multi-layer collection + NaN-σ drop + σ=∞-retained-at-∞ — `marine_bathymetry_store/test/test_query.cpp`
- [ ] (suggestion) ADR citation nuance: σ=∞→unknown-quality mapping is ADR-0010 D4; note attributes it to "(ADR-0002 §D7)" (defensible for the conservative consumer policy, but D4 owns the mapping) — `docs/decisions/0010-geospatial-world-model.md:233`
- [ ] (suggestion) Stale test comments still say "shallowestReliable → nullopt" for the NaN-σ path (now reliableSamples→empty→LETHAL); assertions correct — `bathymetry_layer/test/test_bathymetry_layer.cpp:299,331,345`
