# Plan: marine_sidescan_mosaic: draft-layer newest-valid-wins (recency) compositing policy

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/177

## Context

The live mosaicker (`SidescanMosaicNode`) uses `SplatPolicy::Mean` or `SplatPolicy::MaxHold`
via the `splat` ROS param. For the operator-facing `draft/` layer, the wanted policy is
**recency**: every new ping visually updates the tile so the operator can see the sonar
actively painting. With `Mean`, a second pass over already-covered ground blends in
(visually stagnates); with `Newest` it immediately overwrites.

**Validity predicate (critical):** the normalizer (`RollingNormalizer::normalize`) clamps
real returns to `[1, 65535]` (see `normalizer.cpp:75`). A decoded zero therefore means
no-data / dropout — never a valid real return. The sentinel used by `saveTiles` for
untouched cells is also `0`. Consequently, `Newest` must only overwrite when `value != 0`;
`value == 0` must NOT overwrite existing coverage.

The live node default changes from `mean` to `newest`; existing `splat: mean` / `splat:
max_hold` deployments continue to work unchanged.

## Approach

1. **Add `SplatPolicy::Newest` to `accumulator.hpp`** — extend the enum and update the
   class-level Doxygen comment to describe all three policies. Add a contract note to
   the `add()` docstring: "For `Newest`, a `value == 0` sample is treated as no-data
   and does NOT overwrite existing coverage; real returns (guaranteed ≥ 1 by the
   normalizer's `clamp(scaled, 1.0, 65535.0)` floor) always overwrite."

2. **Implement `Newest` branch in `accumulator.cpp`** — in `add()`, add a third branch
   after the `else` (`MaxHold`) block:
   ```
   } else if (policy_ == SplatPolicy::Newest) {
     // value==0 is the no-data sentinel; real returns are floored to >=1
     // by RollingNormalizer, so skip zero to avoid punching holes in coverage.
     if (value != 0) {
       tile.set(row, col, 0, value);
     }
   }
   ```
   No extra side-state is needed (no sum/count like `Mean`, no held max like `MaxHold`).

3. **Update `mosaic_node.cpp`**:
   - Change the `splat` default in `declare_parameter` from `"mean"` to `"newest"`.
   - Expand the splat parse from a 2-way (`max_hold` vs fallthrough) to a 3-way branch:
     ```cpp
     SplatPolicy sp = SplatPolicy::Newest;  // default
     if (splat == "mean") {
       sp = SplatPolicy::Mean;
     } else if (splat == "max_hold") {
       sp = SplatPolicy::MaxHold;
     } else if (splat != "newest") {
       RCLCPP_WARN(get_logger(),
         "Unknown splat value '%s'; defaulting to 'newest'", splat.c_str());
     }
     accumulator_ = MosaicAccumulator(level_, sp);
     ```

4. **Update `test_accumulator.cpp`** — add two new `TEST` cases:
   - `NewestReplaces`: three adds in sequence (10 → 30 → 20); expect final value is 20
     (strictly last write, not highest).
   - `NewestSkipsZero`: add non-zero, then add zero; expect original value is preserved
     (the zero must not overwrite).

5. **Update `README.md`**:
   - In the `splat` parameter row, change default from `mean` to `newest` and add
     `newest` to the valid-values list: `mean` / `max_hold` / `newest`.
   - Amend the "No-data = 0" bullet in Limitations to note that `Newest` also guards
     against zero-overwrite.

## Files to Change

| File | Change |
|------|--------|
| `include/marine_sidescan_mosaic/accumulator.hpp` | Add `Newest` to `SplatPolicy` enum; update class + `add()` docstrings |
| `src/accumulator.cpp` | Add `Newest` branch in `add()` |
| `src/mosaic_node.cpp` | Change `splat` default to `"newest"`; 3-way parse + WARN for unknown |
| `test/test_accumulator.cpp` | Add `NewestReplaces` and `NewestSkipsZero` tests |
| `README.md` | Add `newest` to param table; note in Limitations |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Fix completely — add the test, handle the edge case | Both the happy-path (`NewestReplaces`) and the zero-guard (`NewestSkipsZero`) are explicit tests |
| Never silent failure | Unknown `splat` value now WARN-logs instead of silently falling through to `Mean` |
| Verify against source, not assumptions | Validity predicate (`value != 0`) pinned to observed `normalizer.cpp:75` clamp, not the issue's loose wording |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 (`draft`/`processed` layer convention) | Yes | `Newest` is for the `draft/` live layer; plan scopes to that only; `processed` layer untouched |
| ADR-0009 (venv for dev tools) | No | No new dev dependencies |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `splat` default (`mean` → `newest`) | PR description must call out the default change so operators know to set `splat: mean` explicitly | Yes — PR description note (step 3) |
| `SplatPolicy` enum | All switch/if-else on the enum in current + future code must handle `Newest` | Yes — `mosaic_node.cpp` parse is the only consumer; updated in step 3 |
| README param table | The "No-data = 0" Limitations bullet | Yes — step 5 |

## Open Questions

- None — validity predicate is confirmed from source code; all action items from the
  Issue Review are folded in.

## Estimated Scope

Single PR; ~80 lines of changes across 5 files. No dependency on any other in-flight
branch.
