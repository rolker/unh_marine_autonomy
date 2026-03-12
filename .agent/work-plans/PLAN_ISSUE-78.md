# Plan: GGGS — correct polar column handling in bounds and iterators

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/78

## Context

The GGGS library uses latitude-dependent column counts: rows below 72° have
the full column count, rows at 72–80° have 1/3 the columns (3x wider grids),
and rows above 80° have 1/9 the columns (9x wider). The infrastructure for
this (`LevelSpecs::columnCount(row)`, `latitudeScaleFactor(row)`) exists and
works correctly. However, three components assume uniform column numbering:

- `min(GridIndex, GridIndex)` / `max(GridIndex, GridIndex)` — component-wise
  min/max of row+column, producing invalid column indices when rows span bands
- `GridBounds::gridColumnCount()` — `1 + max.col - min.col`, meaningless
  across band boundaries
- `GridAreaIterator::next()` — iterates `from.col` to `to.col` on every row,
  visiting nonexistent columns in bands with fewer columns

**Blast radius**: `min`/`max` are called only from `GridBounds::expand()` and
one test. `GridBounds` and `GridAreaIterator` are used only in tests and a doc
example in `gggs.h`. No production code outside the test suite uses these APIs
yet — #86 (bathymetric data store) will be the first consumer.

## Approach

### 1. Fix `GridAreaIterator` to use per-row column bounds

In `next()`, after advancing to a new row, compute that row's valid column
range using `LevelSpecs::columnCount(row)`. Clamp the iteration column to the
valid range for each row.

The key change: when iterating rows that cross a band boundary, the column
range narrows. For a row with `columnCount(row) = C`, valid columns are
`[0, C-1]`. The iterator should intersect the requested column range with
the row's valid range.

**Longitude-aware mapping**: column indices have different geographic meaning
at different latitudes. Column 10 at the equator covers a different longitude
range than column 10 at 75°. The iterator needs to decide: iterate by
**column index** (visiting the same numbered columns, some of which may not
exist) or by **longitude range** (visiting all columns that overlap the
geographic extent). The longitude-range approach is correct for spatial
queries.

Implementation in `grid_area_iterator.h`:
- Store the level (to access `LevelSpecs`)
- In `next()`, when advancing to a new row:
  - Compute the geographic longitude range from `from_` and `to_` columns at
    their respective rows
  - Map that longitude range to column indices at the new row using
    `LevelSpecs::gridLongitudinalSpan(row)`
  - Iterate those columns
- Update `valid()` to check per-row column limits

### 2. Fix `GridBounds` for cross-band awareness

`GridBounds` tracks an axis-aligned bounding box. With varying column counts,
it needs to track the geographic extent rather than raw column indices.

Options (choose one):
- **(A) Store row-based bounds**: keep `min_`/`max_` as `GridIndex` but make
  `gridColumnCount()` return the max across all rows in the range. This is
  simpler but `gridColumnCount()` becomes an approximation.
- **(B) Deprecate `gridColumnCount()`**: remove or replace it with
  `gridColumnCount(uint32_t row)` that returns the per-row count within the
  bounds. `cellColumnCount()` would similarly become per-row.

**Recommended: (B)** — per-row column count is the only correct answer when
bounds span bands. A single `gridColumnCount()` is inherently misleading.

### 3. Fix or restrict `min`/`max` on `GridIndex`

These produce invalid indices when rows are in different bands. Options:
- **(A) Remove them** — callers use `GridBounds::expand()` instead (which
  would handle band differences internally)
- **(B) Keep but throw on cross-band** — add a check that both indices have
  the same `latitudeScaleFactor`
- **(C) Make longitude-aware** — compute geographic min/max and map back to
  indices at each row

**Recommended: (A)** — `min`/`max` on `GridIndex` is conceptually broken for
the cross-band case. `GridBounds::expand()` is the correct API for building
bounding boxes. Remove the friend functions and update the one test caller.

### 4. Update `GridBounds::expand()` to not use `min`/`max`

If `min`/`max` are removed, `expand()` needs to track row bounds and
longitude bounds separately:
- Row: `min(row)` / `max(row)` — straightforward
- Longitude: track west/east longitude extents, then compute per-row column
  ranges when queried

### 5. Add cross-polar tests

Add test cases in `test_gggs.cpp`:
- `GridAreaIterator` spanning 71°–73° (crosses 72° boundary, 1x→3x)
- `GridAreaIterator` spanning 79°–81° (crosses 80° boundary, 3x→9x)
- `GridAreaIterator` spanning 71°–81° (crosses both boundaries)
- Verify every visited index passes `valid()`
- Verify no valid index in the geographic extent is skipped
- `GridBounds` column count queries across band boundaries
- Southern hemisphere equivalents (negative latitudes)

### 6. Update existing tests

The `MultipleGrids` test in `test_gggs.cpp` currently uses `min(gi1, gi2)`
and `max(gi1, gi2)` at lines 480–481. If `min`/`max` are removed, this test
must be updated to use `GridBounds` or construct the iterator differently.

## Files to Change

| File | Change |
|------|--------|
| `include/marine_autonomy/gggs/grid_area_iterator.h` | Longitude-aware per-row column iteration |
| `include/marine_autonomy/gggs/bounds.h` | Per-row column queries; remove `gridColumnCount()` or make it per-row |
| `include/marine_autonomy/gggs/grid_index.h` | Remove `min`/`max` friend functions |
| `test/test_gggs.cpp` | Cross-polar iterator/bounds tests; update `MultipleGrids` test |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First | Correct spatial iteration prevents silent data corruption in downstream #86 |
| Test what breaks | Tests target the exact failure modes: cross-band iteration, column validity |
| A change includes its consequences | Removing `min`/`max` requires updating the one test caller in the same PR |
| Only what's needed | Minimal API surface change — fix the broken parts, keep what works |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 — Worktree isolation | Yes | Working in `feature/issue-78` worktree |
| 0008 — ROS 2 conventions | Marginal | C++ library; follows existing naming patterns |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `min`/`max` friend functions | Test callers (line 480–481) | Yes — step 6 |
| `GridBounds` API | Test callers (5 test functions) | Yes — step 6 |
| `GridAreaIterator` behavior | Existing iterator tests | Yes — step 6 |

## Open Questions

1. **Longitude-range vs column-index iteration**: should `GridAreaIterator`
   iterate by geographic longitude extent (correct for spatial queries) or by
   column index (simpler but breaks across bands)? Recommended: longitude-range.
2. **`GridBounds::gridColumnCount()` removal vs per-row**: should it be
   removed entirely, return the maximum across rows, or take a row parameter?
3. **`min`/`max` removal vs restriction**: remove entirely (recommended) or
   keep with a same-band assertion?

## Estimated Scope

Single PR. All changes are in header files + one test file, contained within
the GGGS module.
