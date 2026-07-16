# Plan: SonarInfo angular-response TL provenance

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/268

## Context

The CUBE estimator's `AngularResponseCurve` (cube_bathymetry#87) is
self-describing about transmission loss: a tier-2 curve is a TL-REMOVED
residual and the consumer must add back `40·log10(R) + 2αR` before
subtracting it. `SonarInfo` (merged today, #240 / PR #266) carries the curve
arrays but not that provenance, so a tier-2 curve read from the message would
be silently mis-applied as tier-1. No curve-bearing bags exist yet, so the
extension is free now.

Part of the curve-delivery arc: producer marine_tools#71, consumer
cube_bathymetry#102 (auto-enable on curve presence, decided 2026-07-16).

## Approach

1. `marine_interfaces/msg/SonarInfo.msg`, correction-state block, directly
   after the angular-response arrays:
   - `angular_response_tl` tri-state (`ANGULAR_RESPONSE_TL_UNKNOWN = 0` /
     `_TL_IN = 1` / `_TL_REMOVED = 2`) — honest zero default per the
     message's conventions.
   - `float32 angular_response_absorption_db_per_m` — NaN if unknown /
     tier-1 (explicit producer obligation, like the other NaN sentinels).
   - Comment notes consumers MUST NOT apply a non-empty curve whose
     provenance is `UNKNOWN` (mis-application corrupts the store).
2. ADR-0009: addendum in the field-set section + a consequences line noting
   the extension landed before any curve-bearing bags (no `.bmr` rule
   needed).
3. Build + lint; `ros2 interface show` renders.

## Out of scope

- Producer/consumer changes (marine_tools#71, cube_bathymetry#102).

## Verification

- `marine_interfaces` builds; 5 lint tests pass; interface renders with the
  new fields.
