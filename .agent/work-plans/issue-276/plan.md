# Plan: Rework bathymetry_layer cost model for chart-grade uncertainty (ADR-0010 D7 precondition)

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/276

## Context

`bathymetry_layer` currently uses `max_uncertainty` as a **reject** filter: cells
where every sample exceeds the gate fail `shallowestReliable` → nullopt → LETHAL.
For survey-grade σ (≤0.5 m) this is correct. For CATZOC-grade chart σ (0.5 m –
several m) it is wrong: chart-only regions become wholesale keepout, or a global
gate relaxation also weakens the filter for noisy draft data.

ADR-0010 D7 names the fix a precondition for chart cells entering the store.
The agreed cost model (2026-06-25): cost is driven by **worst-case clearance** =
clearance − σ; keepout is reserved for trusted data (σ ≤ `confidence_gate`) only;
high-σ cells are costed (caution), never hard-forbidden on their own; σ=∞/NaN data
stays conservative (LETHAL via the existing "data exists but no reliable sample" path).

## Approach

1. **Rename `max_uncertainty` → `confidence_gate`** — rename the ROS parameter and
   the member variable `max_uncertainty_` → `confidence_gate_`. Update `onInitialize`,
   the RCLCPP_INFO startup log, and the README parameter table (with a "was
   `max_uncertainty`" migration note).
   - **Deprecation guard (Plan-Review finding 3).** The rename is not a pure
     rename — the parameter's *meaning* changes from a reject-filter (over-gate
     σ ⇒ LETHAL) to a trust-threshold (σ ≤ gate ⇒ keepout-eligible; over-gate ⇒
     costed caution). A boat config still setting `max_uncertainty:` would be
     silently ignored AND carry the old mental model. So: declare the deprecated
     `max_uncertainty` key with a sentinel (NaN) default and, if `onInitialize`
     finds it was actually set, emit a one-shot `RCLCPP_WARN` naming the rename
     AND the semantic change, and stating the value is ignored. (`onInitialize`
     runs once per lifecycle, so a plain WARN there is inherently one-shot.) A
     protected `deprecated_max_uncertainty_seen_` latch records that the guard
     fired, so a unit test can assert it without scraping logs.
   - The README migration note must call out the **semantic** change
     (reject-filter → trust-threshold), not just the key rename.

2. **Extend `computeCost` signature** — change to
   `computeCost(double worst_case_clearance, bool trusted) const`. When `trusted=true`
   the ramp is unchanged (LETHAL below `minimum_depth_`, FREE above
   `maximum_caution_depth_`, linear ramp between). When `trusted=false` the same ramp
   applies but the result is clamped to `MAX_NON_OBSTACLE` (never LETHAL from
   uncertain data alone).

3. **Rework `evaluateCell`** — after the `bestSource` data-existence check (unchanged):
   - Call `shallowestReliable(*store_, cell, std::numeric_limits<double>::infinity())`
     to get the shallowest sample without a finite-σ reject-filter.
   - **σ=∞ correctness refinement (found during implementation, not in the
     original plan).** `shallowestReliable(∞)` returns nullopt only when *every*
     sample has a **NaN** σ — it does **not** filter a literal **σ=∞** sample,
     because the gate test is `c->uncertainty > max_uncertainty` and `∞ > ∞` is
     `false` (verified `query.cpp:131`). But the D7 model buckets σ=∞ (genuinely
     *unknown* quality, ADR-0010 D4) with no-data, NOT with high-but-finite-σ
     caution. So the conservative path fires on **`!sample ||
     !std::isfinite(sample->uncertainty)`** → LETHAL. That keeps "σ=∞/no-data ⇒
     conservative" intact while "large *finite* σ ⇒ costed caution" is the new
     behavior. (This is why the requirement pairs "σ=∞/no-data" and separates it
     from "high-σ".)
   - Otherwise (finite σ): `worst_case = map_tide_z_ - sample->depth -
     sample->uncertainty`. `trusted = (sample->uncertainty <= confidence_gate_)`.
     Return `computeCost(worst_case, trusted)`.
   - **Worst-case-selection nuance (Plan-Review finding 5), confirmed.**
     `shallowestReliable(∞)` returns the σ of the *shallowest-depth* sample, not
     the min over samples of (clearance − σ); a deeper-but-noisier sample could
     in principle bind tighter. Confirmed against ADR-0010 D7 intent: with **one
     fused surface per layer** (ADR-0002 #221) there is a single sample per cell
     per layer, so the two coincide, and the shallowest-depth sample is also the
     most-hazardous point estimate — consistent with the existing
     shallowest-reliable safety-query design. No behavior change; documented in a
     code comment in `evaluateCell`.

4. **Update tests** —
   - Update all `computeCost` callers (test cases 1, 8) to pass `trusted=true`
     (preserves existing ramp/boundary assertions).
   - Rename the `setMaxUncertainty` helper → `setConfidenceGate`
     (`test_bathymetry_layer.cpp:60`) and update every caller and the comment at
     `:232` (Plan-Review finding 4).
   - **Rewrite the two tests that pinned the OLD reject-filter (found during
     implementation).** `OverUncertainSurveyedCellIsLethal` (σ=5.0, deep) and
     `OverUncertainCellIsLethalWithNoPriorFallback` (first block, σ=5.0) asserted
     that a *finite-but-over-gate* σ cell is LETHAL — exactly the behavior D7
     reverses. Under the new model those cells are costed by worst-case clearance
     (caution / FREE), never LETHAL from σ alone. Reframe them to the new
     semantics (they become the concrete "chart-σ ⇒ caution not LETHAL" and
     "σ=∞ ⇒ conservative LETHAL" cases). The plan's step-4 list originally missed
     these; leaving them would be a build/test failure, and silently deleting
     them would drop coverage of the confident-record control.
   - Add the four issue-named cases:
     - Trusted + worst-case shallow → LETHAL (σ ≤ gate, clearance−σ < minimum_depth).
     - Untrusted + worst-case shallow → caution (`MAX_NON_OBSTACLE`) NOT LETHAL
       (σ > gate, chart-grade).
     - σ=∞ data → LETHAL (conservative path via the isfinite guard); no-data
       paths unchanged (existing tests 2 / 2b / 2c).
     - Ramp continuity: at a fixed worst-case clearance in the ramp band, trusted
       vs untrusted yield the *same* cost (the trust flag only changes the
       below-`minimum_depth` verdict, so the ramp is continuous across the split).
   - Add a deprecation-guard test (positive: `max_uncertainty` set ⇒
     `deprecated_max_uncertainty_seen_` latched true; negative: unset ⇒ false),
     driven through `initialize()` like the existing `TideFrameTest` cases.

5. **Update README** — parameter table: remove `max_uncertainty`, add
   `confidence_gate` with its new (trust-threshold) semantics and a migration note
   that states the semantic change, not just the rename. **Also update the example
   nav2 config block** (`README.md:124`), which carries a second `max_uncertainty`
   occurrence beyond the table (Plan-Review finding 2), plus the "How it works" /
   no-data-policy prose so the described model is the worst-case-clearance one.

## Files to Change

| File | Change |
|------|--------|
| `bathymetry_layer/src/bathymetry_layer.cpp` | `evaluateCell` rework (step 3); `computeCost` new signature (step 2); `onInitialize` parameter rename (step 1) |
| `bathymetry_layer/src/bathymetry_layer.hpp` | `confidence_gate_` member rename; `computeCost` signature; class doc update |
| `bathymetry_layer/test/test_bathymetry_layer.cpp` | Update existing `computeCost` callers; add 4 new test cases (step 4) |
| `bathymetry_layer/README.md` | Parameter table: `confidence_gate` replacing `max_uncertainty`; migration note |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Parameter rename is documented in README; behavior change is explicit in log output |
| Capture decisions, not just implementations | This implements the 2026-06-25 design decision already recorded in ADR-0010 D7 |
| A change includes its consequences | README updated in the same PR; tests cover the new cost-model paths |
| Test what breaks | Tests target the new safety-critical boundaries: trusted keepout, untrusted caution cap, NaN-σ path |
| Only what's needed | No new store machinery; no layer-specific branching; single-PR scope |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D7 | Yes — this PR is that precondition | Implements the worst-case-clearance / confidence-gate cost model exactly as specified |
| ADR-0002 D7 two-query pattern | Yes — `evaluateCell` changes | Two-query structure preserved: `bestSource` still answers "any data?", `shallowestReliable(∞)` answers "shallowest depth+σ"; logic is not collapsed |
| ADR-0008 (ROS 2 conventions) | Yes — parameter rename | Parameter rename in `onInitialize` follows `declareParameter` conventions |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `computeCost` signature | All direct callers (two tests + `evaluateCell`) | Yes |
| `max_uncertainty` parameter name | README migration note; launch files / nav2_params on boat (config migration is operator task, noted in README) | README yes; launch files are operator task — noted in README as migration |
| `evaluateCell` behavior | Tests cover new and old paths | Yes |

## Sim-Validation Decision (Plan-Review finding 1 — resolved)

review-issue actioned plan-task to "identify the sim harness and define the
'sim-validated' criterion per ADR-0002 D7 before field use," and the plan was
originally silent. **Operator decision (2026-07-31, run-issue checkpoint): sim
validation is NON-BLOCKING / advisory for this change.**

Rationale: the cost-model rework is a pure, deterministic function of
(clearance, σ, confidence_gate, ramp bounds) — the safety-critical boundaries
(trusted keepout, untrusted caution cap, σ=∞ conservatism, ramp continuity) are
fully exercised by the unit tests added here, which is where a regression in the
math would show up. A sim integration run adds confidence that the model behaves
in the full costmap/planner loop, but it does **not gate the August deployment**:
the unit tests are the enforced correctness bar. The sim acceptance run stays
tracked as follow-on (README "Follow-on work" → "Sim acceptance run", gated on
the contour-importer / sim-MBES harness, #163), now explicitly marked advisory
rather than a pre-field blocker. This records the decision so the review-issue
action is resolved, not lost.

## Open Questions

- [ ] No open questions — plan is review-plan-ready. The sim-validation gate is
  resolved above (non-blocking); the σ=∞ and old-reject-filter-test items
  surfaced in implementation are folded into steps 3–4.

## Estimated Scope

Single PR. All changes are within `bathymetry_layer`; no store API changes required
(`shallowestReliable` called with `infinity()` is a clean existing affordance).
