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

2. **Extend `computeCost` signature** — change to
   `computeCost(double worst_case_clearance, bool trusted) const`. When `trusted=true`
   the ramp is unchanged (LETHAL below `minimum_depth_`, FREE above
   `maximum_caution_depth_`, linear ramp between). When `trusted=false` the same ramp
   applies but the result is clamped to `MAX_NON_OBSTACLE` (never LETHAL from
   uncertain data alone).

3. **Rework `evaluateCell`** — after the `bestSource` data-existence check (unchanged):
   - Call `shallowestReliable(*store_, cell, std::numeric_limits<double>::infinity())`
     to get the shallowest sample without σ-filtering.
   - If nullopt (all data has NaN uncertainty) → LETHAL (unchanged "data but no reliable
     sample" conservative path).
   - Otherwise: `worst_case = map_tide_z_ - sample->depth - sample->uncertainty`.
     `trusted = (sample->uncertainty <= confidence_gate_)`.
     Return `computeCost(worst_case, trusted)`.

4. **Update tests** — update all `computeCost` callers (test cases 1, 8) to pass
   `trusted=true` (preserves existing assertions). Add four new test cases:
   - Trusted + worst-case shallow → LETHAL (σ ≤ gate, clearance−σ < minimum_depth).
   - Untrusted + worst-case shallow → caution NOT LETHAL (σ > gate, chart-grade).
   - All-NaN-σ data → LETHAL (nullopt from shallowestReliable(∞) → existing path).
   - Ramp continuity: σ at gate boundary → worst_case drives cost normally (no
     discontinuity at the trusted/untrusted split).

5. **Update README parameter table** — remove `max_uncertainty`, add `confidence_gate`
   with its new semantics and the migration note.

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

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR. All changes are within `bathymetry_layer`; no store API changes required
(`shallowestReliable` called with `infinity()` is a clean existing affordance).
