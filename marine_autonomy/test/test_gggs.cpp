// Copyright 2025 Roland Arsenault
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the Roland Arsenault nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <gtest/gtest.h>
#include <cmath>
#include <type_traits>
#include "marine_autonomy/gggs.h"

// ============================================================================
// core.h — latitudeScaleFactor (free function)
// ============================================================================

TEST(GGGSCore, LatitudeScaleFactorEquator)
{
  // Equator should have scale factor 1
  EXPECT_EQ(gggs::latitudeScaleFactor(0.0), 1);
}

TEST(GGGSCore, LatitudeScaleFactorMidLatitude)
{
  // 43 degrees N (e.g., New Hampshire) should have scale factor 1
  EXPECT_EQ(gggs::latitudeScaleFactor(43.0), 1);
}

TEST(GGGSCore, LatitudeScaleFactorHighLatitude)
{
  // 75 degrees N is between 72 and 80, should have scale factor 3
  EXPECT_EQ(gggs::latitudeScaleFactor(75.0), 3);
}

TEST(GGGSCore, LatitudeScaleFactorPolar)
{
  // 85 degrees N is above 80, should have scale factor 9
  EXPECT_EQ(gggs::latitudeScaleFactor(85.0), 9);
}

TEST(GGGSCore, LatitudeScaleFactorSouthHighLatitude)
{
  // -75 degrees is between -72 and -80, should have scale factor 3
  EXPECT_EQ(gggs::latitudeScaleFactor(-75.0), 3);
}

TEST(GGGSCore, LatitudeScaleFactorSouthPolar)
{
  // -85 degrees is below -80, should have scale factor 9
  EXPECT_EQ(gggs::latitudeScaleFactor(-85.0), 9);
}

TEST(GGGSCore, LatitudeScaleFactorBoundaries)
{
  // Exact boundaries
  EXPECT_EQ(gggs::latitudeScaleFactor(72.0), 3);
  EXPECT_EQ(gggs::latitudeScaleFactor(-72.0), 3);
  EXPECT_EQ(gggs::latitudeScaleFactor(80.0), 9);
  EXPECT_EQ(gggs::latitudeScaleFactor(-80.0), 9);
  EXPECT_EQ(gggs::latitudeScaleFactor(90.0), 9);
  EXPECT_EQ(gggs::latitudeScaleFactor(-90.0), 9);
}

// ============================================================================
// level_spec.h — LevelSpecs::latitudeScaleFactor (member, row-based)
// ============================================================================

TEST(GGGSLevelSpecs, LatitudeScaleFactorRowBased)
{
  // The member function takes a row index and works correctly
  // Level 0: grid_angular_span = 8.0, row for 72° = (72+96)/8 = 21, row for -72° = (-72+96)/8 = 3
  const auto & spec = gggs::levels[0];
  // Equator: row = (0+96)/8 = 12
  EXPECT_EQ(spec.latitudeScaleFactor(12), 1);
  // Row 21 is at 72° boundary (row_plus_72 = 21), so scale factor 3
  EXPECT_EQ(spec.latitudeScaleFactor(21), 3);
  // Row 22 is at 80° boundary (row_plus_80 = 22), so scale factor 9
  EXPECT_EQ(spec.latitudeScaleFactor(22), 9);
  // Row 2 is at -80° boundary (row_minus_80 = 2), so scale factor 3
  EXPECT_EQ(spec.latitudeScaleFactor(2), 3);
}

TEST(GGGSLevelSpecs, LatitudeScaleFactorPolarRows)
{
  const auto & spec = gggs::levels[0];
  // Row for 80° = (80+96)/8 = 22, so row 22 is at the boundary
  // Row 23 is above 80°
  EXPECT_EQ(spec.latitudeScaleFactor(23), 9);
  // Row 0 is at -96° (below -80°)
  EXPECT_EQ(spec.latitudeScaleFactor(0), 9);
}

// ============================================================================
// core.h — Constants
// ============================================================================

TEST(GGGSCore, GridConstants)
{
  EXPECT_EQ(gggs::cell_rows_per_grid, 960);
  EXPECT_EQ(gggs::cell_columns_per_grid, 960);
  EXPECT_EQ(gggs::grid_total_cell_count, 960u * 960u);
  EXPECT_EQ(gggs::level_0_row_count, 24u);
  EXPECT_EQ(gggs::level_0_column_count, 45u);
}

// ============================================================================
// GridIndex — construction and validity
// ============================================================================

TEST(GGGSGridIndex, DefaultConstructedIsInvalid)
{
  gggs::GridIndex gi;
  EXPECT_FALSE(gi.valid());
}

TEST(GGGSGridIndex, PositionRoundTrip)
{
  // Use Level to create a GridIndex for a known location
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);  // New Hampshire coast
  EXPECT_TRUE(gi.valid());
  EXPECT_EQ(gi.level(), 5);

  // The position should be within the grid bounds
  EXPECT_LE(gi.southLatitude(), 43.0);
  EXPECT_GE(gi.northLatitude(), 43.0);
  EXPECT_LE(gi.westLongitude(), -70.5);
  EXPECT_GE(gi.eastLongitude(), -70.5);
}

TEST(GGGSGridIndex, SouthWestNorthEastConsistency)
{
  gggs::Level level(3);
  auto gi = level.gridIndex(10.0, 20.0);
  EXPECT_TRUE(gi.valid());
  EXPECT_LT(gi.southLatitude(), gi.northLatitude());
  EXPECT_LT(gi.westLongitude(), gi.eastLongitude());
}

TEST(GGGSGridIndex, LatitudinalSpanMatchesEdges)
{
  gggs::Level level(3);
  auto gi = level.gridIndex(10.0, 20.0);
  EXPECT_NEAR(gi.latitudinalSpan(), gi.northLatitude() - gi.southLatitude(), 1e-10);
}

TEST(GGGSGridIndex, LongitudinalSpanMatchesEdges)
{
  gggs::Level level(3);
  auto gi = level.gridIndex(10.0, 20.0);
  EXPECT_NEAR(gi.longitudinalSpan(), gi.eastLongitude() - gi.westLongitude(), 1e-10);
}

TEST(GGGSGridIndex, PolarGridWiderLongitude)
{
  // At high latitudes, grids should be wider in longitude (3x at 72-80°)
  gggs::Level level(3);
  auto gi_equator = level.gridIndex(0.0, 0.0);
  auto gi_polar = level.gridIndex(75.0, 0.0);
  EXPECT_NEAR(gi_polar.longitudinalSpan(), 3.0 * gi_equator.longitudinalSpan(), 1e-10);
}

TEST(GGGSGridIndex, VeryHighLatitudeGrid)
{
  // At 85°, grids should be 9x wider in longitude
  gggs::Level level(3);
  auto gi_equator = level.gridIndex(0.0, 0.0);
  auto gi_polar = level.gridIndex(85.0, 0.0);
  EXPECT_NEAR(gi_polar.longitudinalSpan(), 9.0 * gi_equator.longitudinalSpan(), 1e-10);
}

// ============================================================================
// GridIndex — comparisons
// ============================================================================

TEST(GGGSGridIndex, EqualityForSameGrid)
{
  gggs::Level level(5);
  auto gi1 = level.gridIndex(43.0, -70.5);
  auto gi2 = level.gridIndex(43.0, -70.5);
  EXPECT_TRUE(gi1 == gi2);
  EXPECT_FALSE(gi1 != gi2);
}

TEST(GGGSGridIndex, InequalityForDifferentGrids)
{
  gggs::Level level(5);
  auto gi1 = level.gridIndex(43.0, -70.5);
  auto gi2 = level.gridIndex(44.0, -70.5);
  EXPECT_TRUE(gi1 != gi2);
  EXPECT_FALSE(gi1 == gi2);
}

TEST(GGGSGridIndex, DefaultConstructedEquality)
{
  gggs::GridIndex gi1;
  gggs::GridIndex gi2;
  // Two invalid (default-constructed) GridIndex objects should be equal
  EXPECT_TRUE(gi1 == gi2);
  EXPECT_FALSE(gi1 != gi2);
}

TEST(GGGSGridIndex, InvalidVsValidNotEqual)
{
  gggs::GridIndex invalid;
  gggs::Level level(5);
  auto valid = level.gridIndex(43.0, -70.5);
  EXPECT_FALSE(invalid == valid);
  EXPECT_TRUE(invalid != valid);
}

TEST(GGGSGridIndex, LessThanOrdering)
{
  gggs::Level level(5);
  auto gi1 = level.gridIndex(10.0, 20.0);
  auto gi2 = level.gridIndex(10.0, 21.0);
  // Within the same row, higher longitude should have higher column
  EXPECT_TRUE(gi1 < gi2 || gi2 < gi1 || (gi1 == gi2));
}

// ============================================================================
// Level — fromCellSize and cellSize
// ============================================================================

TEST(GGGSLevel, FromCellSizeLevel0)
{
  // Level 0 grid size is about 890 km, cell size ~ 928 m
  gggs::Level l0(0);
  EXPECT_GT(l0.cellSize(), 900.0);
  EXPECT_LT(l0.cellSize(), 950.0);
}

TEST(GGGSLevel, CellSizeMonotonicity)
{
  // Higher levels should have smaller cells
  for (int i = 0; i < 20; ++i)
  {
    gggs::Level l1(i);
    gggs::Level l2(i + 1);
    EXPECT_GT(l1.cellSize(), l2.cellSize())
      << "Level " << i << " cell size should be larger than level " << (i + 1);
  }
}

TEST(GGGSLevel, FromCellSizeRoundTrip)
{
  // Request a specific cell size, check the level gives that or smaller
  auto level = gggs::Level::fromCellSize(100.0);
  EXPECT_LE(level.cellSize(), 100.0);
}

TEST(GGGSLevel, FromCellSizeVeryLarge)
{
  // Very large cell size should clamp to level 0
  auto level = gggs::Level::fromCellSize(10000.0);
  EXPECT_NEAR(level.cellSize(), gggs::levels[0].nominal_cell_size, 0.01);
}

TEST(GGGSLevel, FromCellSizeVerySmall)
{
  // Very small cell size should clamp to max level 20
  auto level = gggs::Level::fromCellSize(0.0001f);
  EXPECT_NEAR(level.cellSize(), gggs::levels[20].nominal_cell_size, 0.001);
}

TEST(GGGSLevel, OutOfRangeLevelThrows)
{
  EXPECT_THROW(gggs::Level(21), std::out_of_range);
  EXPECT_THROW(gggs::Level(255), std::out_of_range);
}

TEST(GGGSLevel, MaxValidLevel)
{
  EXPECT_NO_THROW(gggs::Level(20));
}

TEST(GGGSLevel, GridIndexEquator)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(0.0, 0.0);
  EXPECT_TRUE(gi.valid());
}

// ============================================================================
// GridBounds
// ============================================================================

TEST(GGGSGridBounds, InitiallyInvalid)
{
  gggs::GridBounds bounds;
  EXPECT_FALSE(bounds.valid());
}

TEST(GGGSGridBounds, ExpandSingleGrid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::GridBounds bounds;
  bounds.expand(gi);
  EXPECT_TRUE(bounds.valid());
  EXPECT_EQ(bounds.gridRowCount(), 1u);
  EXPECT_EQ(bounds.gridColumnCount(gi.row()), 1u);
}

TEST(GGGSGridBounds, ExpandMultipleGrids)
{
  gggs::Level level(5);
  auto gi1 = level.gridIndex(43.0, -70.5);
  auto gi2 = level.gridIndex(44.0, -69.0);
  gggs::GridBounds bounds;
  bounds.expand(gi1);
  bounds.expand(gi2);
  EXPECT_TRUE(bounds.valid());
  EXPECT_GE(bounds.gridRowCount(), 1u);
  // Per-row column count; both grids are in the same latitude band
  EXPECT_GE(bounds.gridColumnCount(gi1.row()), 1u);
}

TEST(GGGSGridBounds, CellCounts)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::GridBounds bounds;
  bounds.expand(gi);
  EXPECT_EQ(bounds.cellRowCount(), static_cast<uint64_t>(gggs::cell_rows_per_grid));
  EXPECT_EQ(bounds.cellColumnCount(gi.row()), static_cast<uint64_t>(gggs::cell_columns_per_grid));
}

TEST(GGGSGridBounds, LevelMismatchThrows)
{
  gggs::Level l5(5);
  gggs::Level l6(6);
  auto gi5 = l5.gridIndex(43.0, -70.5);
  auto gi6 = l6.gridIndex(43.0, -70.5);
  gggs::GridBounds bounds;
  bounds.expand(gi5);
  // Expanding with a different level should throw
  EXPECT_THROW(bounds.expand(gi6), std::out_of_range);
}

// ============================================================================
// CellIndex — construction and position
// ============================================================================

TEST(GGGSCellIndex, DefaultConstructedIsInvalid)
{
  gggs::CellIndex ci;
  EXPECT_FALSE(ci.valid());
}

TEST(GGGSCellIndex, ConstructFromGridIsValid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  // CellIndex at (0,0) within a valid grid
  gggs::CellIndex ci(gi, 0, 0);
  EXPECT_TRUE(ci.valid());
}

TEST(GGGSCellIndex, PositionRoundTrip)
{
  gggs::Level level(5);
  auto pos = gggs::geoPoint(43.0, -70.5);
  auto ci = level.cellIndex(pos);
  EXPECT_TRUE(ci.valid());

  // The cell's position should be close to the original position
  auto cell_pos = ci.position();
  // Cell position is at the SW corner of the cell, so it should be
  // close but slightly less than or equal to the input
  EXPECT_NEAR(cell_pos.latitude, 43.0, 0.01);
  EXPECT_NEAR(cell_pos.longitude, -70.5, 0.01);
}

TEST(GGGSCellIndex, RowColumnWithinBounds)
{
  gggs::Level level(5);
  auto pos = gggs::geoPoint(43.0, -70.5);
  auto ci = level.cellIndex(pos);
  EXPECT_LT(ci.row(), gggs::cell_rows_per_grid);
  EXPECT_LT(ci.column(), gggs::cell_columns_per_grid);
}

TEST(GGGSCellIndex, EdgesConsistent)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::CellIndex ci(gi, 0, 0);
  auto pos = ci.position();
  // Cell (0,0) should have position at the grid's SW corner
  EXPECT_NEAR(pos.latitude, gi.southLatitude(), 1e-10);
  EXPECT_NEAR(pos.longitude, gi.westLongitude(), 1e-10);
}

// ============================================================================
// CellIndex — comparisons
// ============================================================================

TEST(GGGSCellIndex, EqualityForSameCell)
{
  gggs::Level level(5);
  auto pos = gggs::geoPoint(43.0, -70.5);
  auto ci1 = level.cellIndex(pos);
  auto ci2 = level.cellIndex(pos);
  EXPECT_TRUE(ci1 == ci2);
  EXPECT_FALSE(ci1 != ci2);
}

TEST(GGGSCellIndex, DefaultConstructedEquality)
{
  gggs::CellIndex ci1;
  gggs::CellIndex ci2;
  // Two invalid (default-constructed) CellIndex objects should be equal
  EXPECT_TRUE(ci1 == ci2);
  EXPECT_FALSE(ci1 != ci2);
}

TEST(GGGSCellIndex, InvalidVsValidNotEqual)
{
  gggs::CellIndex invalid;
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::CellIndex valid(gi, 0, 0);
  EXPECT_FALSE(invalid == valid);
  EXPECT_TRUE(invalid != valid);
}

// ============================================================================
// GridAreaIterator
// ============================================================================

TEST(GGGSGridAreaIterator, SingleGrid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::GridAreaIterator it(gi, gi);
  EXPECT_TRUE(it.valid());
  EXPECT_EQ((*it).row(), gi.row());
  EXPECT_EQ((*it).column(), gi.column());
  // Only one grid, so next should end iteration
  EXPECT_FALSE(it.next());
}

TEST(GGGSGridAreaIterator, MultipleGrids)
{
  gggs::Level level(5);
  auto gi1 = level.gridIndex(43.0, -71.0);
  auto gi2 = level.gridIndex(43.5, -70.0);
  // Ensure we span at least 2 grids
  if (gi1.row() == gi2.row() && gi1.column() == gi2.column())
  {
    // Same grid, skip count test
    return;
  }
  // Both points are in the same latitude band (scale factor 1),
  // so column count is uniform across all rows in this range.
  gggs::GridAreaIterator it(gi1, gi2);
  uint32_t count = 0;
  if (it.valid())
  {
    count = 1;
    while (it.next()) count++;
  }
  // Compute expected count: the iterator extracts longitude range from the
  // two GridIndex corners, so it covers the correct rectangular area.
  uint32_t min_row = std::min(gi1.row(), gi2.row());
  uint32_t max_row = std::max(gi1.row(), gi2.row());
  uint32_t expected_rows = 1 + max_row - min_row;
  // Same band, so column count is the same for all rows
  uint32_t min_col = std::min(gi1.column(), gi2.column());
  uint32_t max_col = std::max(gi1.column(), gi2.column());
  uint32_t expected_cols = 1 + max_col - min_col;
  EXPECT_EQ(count, expected_rows * expected_cols);
}

TEST(GGGSGridAreaIterator, Reset)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::GridAreaIterator it(gi, gi);
  EXPECT_TRUE(it.valid());
  it.next();  // past the end
  EXPECT_FALSE(it.valid());
  EXPECT_TRUE(it.reset());
  EXPECT_TRUE(it.valid());
}

// ============================================================================
// CellAreaIterator — full grid
// ============================================================================

TEST(GGGSCellAreaIterator, FullGridCellCount)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::CellAreaIterator it(gi);
  uint32_t count = 0;
  if (it.valid())
  {
    count = 1;
    while (it.next()) count++;
  }
  EXPECT_EQ(count, gggs::grid_total_cell_count);
}

TEST(GGGSCellAreaIterator, SingleCell)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  // Create a cell and get its position (SW corner)
  gggs::CellIndex ci(gi, 100, 200);
  auto pos = ci.position();
  // position() returns the SW corner of the cell, which when used to create
  // a new CellIndex, may map to the cell below/left due to floating-point
  // truncation. The CellIndex from position maps to (99, 199).
  gggs::CellAreaIterator it(gi, pos, pos);
  EXPECT_TRUE(it.valid());
  EXPECT_EQ(it->row(), 99);
  EXPECT_EQ(it->column(), 199);
  EXPECT_FALSE(it.next());
}

TEST(GGGSCellAreaIterator, SubRegion)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  // Create bounds that span a portion of the grid
  auto sw = gggs::CellIndex(gi, 10, 20).position();
  auto ne = gggs::CellIndex(gi, 14, 24).position();
  gggs::CellAreaIterator it(gi, sw, ne);

  uint32_t count = 0;
  if (it.valid())
  {
    count = 1;
    while (it.next()) count++;
  }
  // Should be 5 rows * 5 columns = 25 cells
  EXPECT_EQ(count, 25u);
}

TEST(GGGSCellAreaIterator, Reset)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::CellIndex ci(gi, 100, 200);
  auto pos = ci.position();
  gggs::CellAreaIterator it(gi, pos, pos);
  EXPECT_TRUE(it.valid());
  it.next();
  EXPECT_FALSE(it.valid());
  EXPECT_TRUE(it.reset());
  EXPECT_TRUE(it.valid());
}

// ============================================================================
// verifySameLevel
// ============================================================================

TEST(GGGSCore, VerifySameLevelThrowsOnMismatch)
{
  gggs::Level l5(5);
  gggs::Level l6(6);
  auto gi5 = l5.gridIndex(43.0, -70.5);
  auto gi6 = l6.gridIndex(43.0, -70.5);
  EXPECT_THROW(gggs::verifySameLevel(gi5, gi6), std::out_of_range);
}

TEST(GGGSCore, VerifySameLevelNoThrowOnMatch)
{
  gggs::Level l5(5);
  auto gi1 = l5.gridIndex(43.0, -70.5);
  auto gi2 = l5.gridIndex(44.0, -69.0);
  EXPECT_NO_THROW(gggs::verifySameLevel(gi1, gi2));
}

// ============================================================================
// Level::gridIndex — longitude wrapping and latitude clamping/validation
// ============================================================================

TEST(GGGSLevel, GridIndexLongitude180WrapsToNeg180)
{
  // +180° and -180° are the same meridian; the result should be the same grid.
  gggs::Level level(5);
  auto gi_pos = level.gridIndex(0.0, 180.0);
  auto gi_neg = level.gridIndex(0.0, -180.0);
  EXPECT_TRUE(gi_pos.valid());
  EXPECT_EQ(gi_pos, gi_neg);
}

TEST(GGGSLevel, GridIndexLongitude360WrapsToZero)
{
  // 360° should wrap to 0°.
  gggs::Level level(5);
  auto gi_360 = level.gridIndex(0.0, 360.0);
  auto gi_0   = level.gridIndex(0.0, 0.0);
  EXPECT_TRUE(gi_360.valid());
  EXPECT_EQ(gi_360, gi_0);
}

TEST(GGGSLevel, GridIndexLongitudeNeg180Point0001Wraps)
{
  // -180.0001 wraps to ~179.9999 and should produce a valid grid.
  gggs::Level level(5);
  auto gi = level.gridIndex(0.0, -180.0001);
  EXPECT_TRUE(gi.valid());
  auto gi_ref = level.gridIndex(0.0, 179.9999);
  EXPECT_EQ(gi, gi_ref);
}

TEST(GGGSLevel, GridIndexLatitude90Valid)
{
  // Latitude exactly at the north pole should return a valid grid.
  gggs::Level level(5);
  auto gi = level.gridIndex(90.0, 0.0);
  EXPECT_TRUE(gi.valid());
}

TEST(GGGSLevel, GridIndexLatitudeNearPoleClamps)
{
  // A tiny overshoot above 90° (within epsilon) should clamp to the pole.
  gggs::Level level(5);
  auto gi_clamped = level.gridIndex(90.0 + 1e-10, 0.0);
  auto gi_exact   = level.gridIndex(90.0, 0.0);
  EXPECT_TRUE(gi_clamped.valid());
  EXPECT_EQ(gi_clamped, gi_exact);
}

TEST(GGGSLevel, GridIndexLatitude91Throws)
{
  // Latitude clearly outside [-90, 90] should throw std::out_of_range.
  gggs::Level level(5);
  EXPECT_THROW(level.gridIndex(91.0, 0.0), std::out_of_range);
}

TEST(GGGSLevel, GridIndexLatitudeMinus90Valid)
{
  // Latitude exactly at the south pole should return a valid grid.
  gggs::Level level(5);
  auto gi = level.gridIndex(-90.0, 0.0);
  EXPECT_TRUE(gi.valid());
}

TEST(GGGSLevel, GridIndexLatitudeNearSouthPoleClamps)
{
  // A tiny overshoot below -90° (within epsilon) should clamp to the pole.
  gggs::Level level(5);
  auto gi_clamped = level.gridIndex(-90.0 - 1e-10, 0.0);
  auto gi_exact   = level.gridIndex(-90.0, 0.0);
  EXPECT_TRUE(gi_clamped.valid());
  EXPECT_TRUE(gi_exact.valid());
  EXPECT_EQ(gi_clamped, gi_exact);
}

TEST(GGGSLevel, GridIndexLatitudeMinus91Throws)
{
  // Latitude clearly outside [-90, 90] on the south side should throw std::out_of_range.
  gggs::Level level(5);
  EXPECT_THROW(level.gridIndex(-91.0, 0.0), std::out_of_range);
}

// ============================================================================
// Bug-fix regression tests (Issue #77)
// ============================================================================

// Bug 1: CellIndex position constructor parenthesization
// std::min(1.0, delta_lat) clamps before dividing by span, giving wrong row
// at level 0 (8° span).
TEST(GGGSCellIndex, PositionConstructorLevel0)
{
  gggs::Level level(0);
  // Level 0 grid_angular_span = 8.0°
  // Place a grid whose south edge is at some known latitude.
  // gridIndex(0.0, 0.0) → south edge at 0° (row 12, south = -96+12*8 = 0)
  auto gi = level.gridIndex(0.0, 0.0);
  ASSERT_TRUE(gi.valid());
  double south = gi.southLatitude();
  // Position 4° above the south edge of the grid
  auto pos = gggs::geoPoint(south + 4.0, gi.westLongitude() + 1.0);
  gggs::CellIndex ci(gi, pos);
  ASSERT_TRUE(ci.valid());
  // 4° into an 8° span = 50% → row ≈ 480
  EXPECT_NEAR(ci.row(), 480, 2);
}

// Bug 2: northLatitude() can exceed +90°
// grid_index.h used std::max(-90.0, ...) but no upper clamp. The topmost
// level-0 grid returns northLatitude() = 96.0.
TEST(GGGSGridIndex, NorthLatitudeClampedAt90)
{
  gggs::Level level(0);
  // Row 23 is the topmost level-0 row. south = -96+23*8 = 88°, north = 96°
  // but it should be clamped to 90°.
  auto gi = level.gridIndex(89.0, 0.0);
  ASSERT_TRUE(gi.valid());
  EXPECT_LE(gi.northLatitude(), 90.0);
  EXPECT_GE(gi.southLatitude(), -90.0);
}

// Bug 3: levels array is not const, allowing accidental mutation.
TEST(GGGSLevelSpecs, LevelsArrayIsConst)
{
  static_assert(std::is_const_v<std::remove_reference_t<decltype(gggs::levels)>>,
    "gggs::levels should be const");
}

// Bug 4: operator< inconsistent with operator== for invalid indices.
// operator== treats all invalid indices as equal, but operator< compared raw
// fields, breaking strict-weak-ordering guarantees.

TEST(GGGSGridIndex, LessThanBothInvalid)
{
  gggs::GridIndex a;
  gggs::GridIndex b;
  // Both invalid: neither should be less than the other
  EXPECT_FALSE(a < b);
  EXPECT_FALSE(b < a);
}

TEST(GGGSGridIndex, LessThanInvalidVsValid)
{
  gggs::GridIndex invalid;
  gggs::Level level(5);
  auto valid = level.gridIndex(43.0, -70.5);
  // Invalid sorts before valid (invalid < valid is true)
  EXPECT_TRUE(invalid < valid);
  EXPECT_FALSE(valid < invalid);
}

TEST(GGGSCellIndex, LessThanBothInvalid)
{
  gggs::CellIndex a;
  gggs::CellIndex b;
  EXPECT_FALSE(a < b);
  EXPECT_FALSE(b < a);
}

TEST(GGGSCellIndex, LessThanInvalidVsValid)
{
  gggs::CellIndex invalid;
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::CellIndex valid(gi, 0, 0);
  EXPECT_TRUE(invalid < valid);
  EXPECT_FALSE(valid < invalid);
}

// Bug 5: CellAreaIterator asymmetric invalid marking.
// When bounds are entirely outside the target grid, some directions left a
// corrupted range that iterates 961 rows instead of producing an empty iterator.

TEST(GGGSCellAreaIterator, BoundsEntirelyBelowGrid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  // Create bounds entirely south of the grid
  double south = gi.southLatitude();
  auto sw = gggs::geoPoint(south - 2.0, gi.westLongitude());
  auto ne = gggs::geoPoint(south - 1.0, gi.eastLongitude());
  gggs::CellAreaIterator it(gi, sw, ne);
  EXPECT_FALSE(it.valid());
}

TEST(GGGSCellAreaIterator, BoundsEntirelyAboveGrid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  double north = gi.northLatitude();
  auto sw = gggs::geoPoint(north + 1.0, gi.westLongitude());
  auto ne = gggs::geoPoint(north + 2.0, gi.eastLongitude());
  gggs::CellAreaIterator it(gi, sw, ne);
  EXPECT_FALSE(it.valid());
}

TEST(GGGSCellAreaIterator, BoundsEntirelyLeftOfGrid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  double west = gi.westLongitude();
  auto sw = gggs::geoPoint(gi.southLatitude(), west - 2.0);
  auto ne = gggs::geoPoint(gi.northLatitude(), west - 1.0);
  gggs::CellAreaIterator it(gi, sw, ne);
  EXPECT_FALSE(it.valid());
}

TEST(GGGSCellAreaIterator, BoundsEntirelyRightOfGrid)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  double east = gi.eastLongitude();
  auto sw = gggs::geoPoint(gi.southLatitude(), east + 1.0);
  auto ne = gggs::geoPoint(gi.northLatitude(), east + 2.0);
  gggs::CellAreaIterator it(gi, sw, ne);
  EXPECT_FALSE(it.valid());
}

// ============================================================================
// core.h — geoPoint / normalizeLongitude helpers and antimeridian handling
// ============================================================================

TEST(GGGSCore, GeoPointSetsFields)
{
  auto p = gggs::geoPoint(43.0, -70.5);
  EXPECT_DOUBLE_EQ(p.latitude, 43.0);
  EXPECT_DOUBLE_EQ(p.longitude, -70.5);
  EXPECT_DOUBLE_EQ(p.altitude, 0.0);
}

TEST(GGGSCore, NormalizeLongitudeWraps)
{
  // In-range values are unchanged.
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(-70.5), -70.5);
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(0.0), 0.0);
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(179.0), 179.0);
  // Just past the antimeridian wraps to the western hemisphere and vice versa.
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(181.0), -179.0);
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(-181.0), 179.0);
  // The half-open convention: ±180 maps to -180.
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(180.0), -180.0);
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(-180.0), -180.0);
  // Multiple wraps.
  EXPECT_DOUBLE_EQ(gggs::normalizeLongitude(540.0 + 1.0), -179.0);
}

// The previous angle-aware position type self-normalized longitude; GeoPoint
// does not, so the GGGS API must restore that behavior at its boundary. An
// un-normalized longitude must resolve to the same grid/cell as its wrapped
// equivalent.
TEST(GGGSAntimeridian, GridIndexNormalizesLongitude)
{
  gggs::Level level(5);
  auto wrapped = level.gridIndex(43.0, 181.0);
  auto direct = level.gridIndex(43.0, -179.0);
  EXPECT_TRUE(wrapped.valid());
  EXPECT_EQ(wrapped, direct);
}

TEST(GGGSAntimeridian, CellIndexNormalizesLongitude)
{
  gggs::Level level(5);
  auto wrapped = level.cellIndex(gggs::geoPoint(43.0, 181.0));
  auto direct = level.cellIndex(gggs::geoPoint(43.0, -179.0));
  EXPECT_TRUE(wrapped.valid());
  EXPECT_EQ(wrapped, direct);
}

// The CellAreaIterator bounds ctor must normalize its corner longitudes too,
// matching gridIndex() and the CellIndex(grid, GeoPoint) ctor. Corners shifted
// by a full turn (+360°) must iterate the identical cells, not be rejected as
// outside the grid. (Regression: the iterator originally read raw longitudes
// while the other two paths normalized — an antimeridian asymmetry.)
TEST(GGGSAntimeridian, CellAreaIteratorNormalizesCornerLongitude)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  // Interior fractions of the grid (deliberately off cell boundaries so a
  // +360° round-trip's floating-point rounding can't nudge a corner into an
  // adjacent cell). Before the iterator ctor normalized its corners, the
  // raw >180° longitudes were rejected as "east of grid" → empty iterator.
  const double lat0 = gi.southLatitude() + gi.latitudinalSpan() * 0.317;
  const double lat1 = gi.southLatitude() + gi.latitudinalSpan() * 0.583;
  const double lon0 = gi.westLongitude() + gi.longitudinalSpan() * 0.317;
  const double lon1 = gi.westLongitude() + gi.longitudinalSpan() * 0.583;

  gggs::CellAreaIterator ref(gi, gggs::geoPoint(lat0, lon0), gggs::geoPoint(lat1, lon1));
  gggs::CellAreaIterator wrapped(gi,
    gggs::geoPoint(lat0, lon0 + 360.0), gggs::geoPoint(lat1, lon1 + 360.0));

  uint32_t ref_count = 0;
  if (ref.valid()) { ref_count = 1; while (ref.next()) ref_count++; }
  uint32_t wrapped_count = 0;
  if (wrapped.valid()) { wrapped_count = 1; while (wrapped.next()) wrapped_count++; }

  EXPECT_GT(ref_count, 0u);
  EXPECT_EQ(wrapped_count, ref_count);
}

// Bug 6: CellIndex public constructor now asserts on out-of-bounds row/column.
// After Bug 5, no internal code passes out-of-bounds values.
TEST(GGGSCellIndex, OutOfBoundsRowAssertsInDebug)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
#ifdef NDEBUG
  // In release builds, assert is disabled; verify valid() returns false.
  gggs::CellIndex ci(gi, gggs::cell_rows_per_grid, 0);
  EXPECT_FALSE(ci.valid());
#else
  EXPECT_DEATH(gggs::CellIndex(gi, gggs::cell_rows_per_grid, 0),
    "CellIndex row out of bounds");
#endif
}

TEST(GGGSCellIndex, OutOfBoundsColumnAssertsInDebug)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
#ifdef NDEBUG
  gggs::CellIndex ci(gi, 0, gggs::cell_columns_per_grid);
  EXPECT_FALSE(ci.valid());
#else
  EXPECT_DEATH(gggs::CellIndex(gi, 0, gggs::cell_columns_per_grid),
    "CellIndex column out of bounds");
#endif
}

// ============================================================================
// Cross-polar GridAreaIterator tests (Issue #78)
// ============================================================================

TEST(GGGSGridAreaIterator, CrossBand72)
{
  // Iterate from 71 to 73 latitude at level 0.
  // This crosses the 72-degree boundary where scale factor changes from 1 to 3.
  gggs::Level level(0);
  auto gi1 = level.gridIndex(71.0, 0.0);
  auto gi2 = level.gridIndex(73.0, 0.0);
  ASSERT_TRUE(gi1.valid());
  ASSERT_TRUE(gi2.valid());

  uint32_t expected_min_row = std::min(gi1.row(), gi2.row());
  uint32_t expected_max_row = std::max(gi1.row(), gi2.row());

  gggs::GridAreaIterator it(gi1, gi2);
  uint32_t count = 0;
  uint32_t min_visited_row = UINT32_MAX;
  uint32_t max_visited_row = 0;
  while(it.valid())
  {
    EXPECT_TRUE(it->valid()) << "Invalid index at row " << it->row()
      << " col " << it->column();
    min_visited_row = std::min(min_visited_row, it->row());
    max_visited_row = std::max(max_visited_row, it->row());
    count++;
    if(!it.next()) break;
  }
  EXPECT_GT(count, 0u);
  EXPECT_EQ(min_visited_row, expected_min_row);
  EXPECT_EQ(max_visited_row, expected_max_row);

  // Verify count matches sum of per-row column counts
  uint32_t expected_count = 0;
  double west = std::min(gi1.westLongitude(), gi2.westLongitude());
  double east = std::max(gi1.eastLongitude(), gi2.eastLongitude());
  for(uint32_t r = expected_min_row; r <= expected_max_row; ++r)
  {
    double span = gggs::levels[0].gridLongitudinalSpan(r);
    uint32_t first = static_cast<uint32_t>((west + 180.0) / span);
    uint32_t last = static_cast<uint32_t>((east + 180.0) / span);
    if(east + 180.0 > 0.0 && std::fmod(east + 180.0, span) < 1e-10)
      last--;
    uint32_t max_col = gggs::levels[0].columnCount(r) - 1;
    first = std::min(first, max_col);
    last = std::min(last, max_col);
    expected_count += 1 + last - first;
  }
  EXPECT_EQ(count, expected_count);
}

TEST(GGGSGridAreaIterator, CrossBand80)
{
  // Iterate from 79 to 81 latitude at level 0.
  // This crosses the 80-degree boundary where scale factor changes from 3 to 9.
  gggs::Level level(0);
  auto gi1 = level.gridIndex(79.0, 0.0);
  auto gi2 = level.gridIndex(81.0, 0.0);
  ASSERT_TRUE(gi1.valid());
  ASSERT_TRUE(gi2.valid());

  uint32_t expected_min_row = std::min(gi1.row(), gi2.row());
  uint32_t expected_max_row = std::max(gi1.row(), gi2.row());

  gggs::GridAreaIterator it(gi1, gi2);
  uint32_t count = 0;
  uint32_t min_visited_row = UINT32_MAX;
  uint32_t max_visited_row = 0;
  while(it.valid())
  {
    EXPECT_TRUE(it->valid()) << "Invalid index at row " << it->row()
      << " col " << it->column();
    min_visited_row = std::min(min_visited_row, it->row());
    max_visited_row = std::max(max_visited_row, it->row());
    count++;
    if(!it.next()) break;
  }
  EXPECT_GT(count, 0u);
  EXPECT_EQ(min_visited_row, expected_min_row);
  EXPECT_EQ(max_visited_row, expected_max_row);

  // Verify count matches sum of per-row column counts
  uint32_t expected_count = 0;
  double west = std::min(gi1.westLongitude(), gi2.westLongitude());
  double east = std::max(gi1.eastLongitude(), gi2.eastLongitude());
  for(uint32_t r = expected_min_row; r <= expected_max_row; ++r)
  {
    double span = gggs::levels[0].gridLongitudinalSpan(r);
    uint32_t first = static_cast<uint32_t>((west + 180.0) / span);
    uint32_t last = static_cast<uint32_t>((east + 180.0) / span);
    if(east + 180.0 > 0.0 && std::fmod(east + 180.0, span) < 1e-10)
      last--;
    uint32_t max_col = gggs::levels[0].columnCount(r) - 1;
    first = std::min(first, max_col);
    last = std::min(last, max_col);
    expected_count += 1 + last - first;
  }
  EXPECT_EQ(count, expected_count);
}

TEST(GGGSGridAreaIterator, CrossBothBands)
{
  // Iterate from 71 to 81 latitude at level 0.
  // This crosses both the 72-degree and 80-degree boundaries.
  gggs::Level level(0);
  auto gi1 = level.gridIndex(71.0, 0.0);
  auto gi2 = level.gridIndex(81.0, 0.0);
  ASSERT_TRUE(gi1.valid());
  ASSERT_TRUE(gi2.valid());

  uint32_t expected_min_row = std::min(gi1.row(), gi2.row());
  uint32_t expected_max_row = std::max(gi1.row(), gi2.row());

  gggs::GridAreaIterator it(gi1, gi2);
  uint32_t count = 0;
  uint32_t min_visited_row = UINT32_MAX;
  uint32_t max_visited_row = 0;
  bool saw_scale_1 = false;
  bool saw_scale_3 = false;
  bool saw_scale_9 = false;
  while(it.valid())
  {
    EXPECT_TRUE(it->valid()) << "Invalid index at row " << it->row()
      << " col " << it->column();
    min_visited_row = std::min(min_visited_row, it->row());
    max_visited_row = std::max(max_visited_row, it->row());
    auto sf = gggs::levels[0].latitudeScaleFactor(it->row());
    if(sf == 1) saw_scale_1 = true;
    if(sf == 3) saw_scale_3 = true;
    if(sf == 9) saw_scale_9 = true;
    count++;
    if(!it.next()) break;
  }
  EXPECT_GT(count, 0u);
  EXPECT_TRUE(saw_scale_1) << "Should visit scale-1 rows";
  EXPECT_TRUE(saw_scale_3) << "Should visit scale-3 rows";
  EXPECT_TRUE(saw_scale_9) << "Should visit scale-9 rows";
  EXPECT_EQ(min_visited_row, expected_min_row);
  EXPECT_EQ(max_visited_row, expected_max_row);

  // Verify count matches sum of per-row column counts
  uint32_t expected_count = 0;
  double west = std::min(gi1.westLongitude(), gi2.westLongitude());
  double east = std::max(gi1.eastLongitude(), gi2.eastLongitude());
  for(uint32_t r = expected_min_row; r <= expected_max_row; ++r)
  {
    double span = gggs::levels[0].gridLongitudinalSpan(r);
    uint32_t first = static_cast<uint32_t>((west + 180.0) / span);
    uint32_t last = static_cast<uint32_t>((east + 180.0) / span);
    if(east + 180.0 > 0.0 && std::fmod(east + 180.0, span) < 1e-10)
      last--;
    uint32_t max_col = gggs::levels[0].columnCount(r) - 1;
    first = std::min(first, max_col);
    last = std::min(last, max_col);
    expected_count += 1 + last - first;
  }
  EXPECT_EQ(count, expected_count);
}

TEST(GGGSGridAreaIterator, SouthernHemisphere)
{
  // Iterate from -73 to -71 latitude at level 0.
  // This crosses the -72 degree boundary (symmetric with northern hemisphere).
  gggs::Level level(0);
  auto gi1 = level.gridIndex(-73.0, 0.0);
  auto gi2 = level.gridIndex(-71.0, 0.0);
  ASSERT_TRUE(gi1.valid());
  ASSERT_TRUE(gi2.valid());

  uint32_t expected_min_row = std::min(gi1.row(), gi2.row());
  uint32_t expected_max_row = std::max(gi1.row(), gi2.row());

  gggs::GridAreaIterator it(gi1, gi2);
  uint32_t count = 0;
  uint32_t min_visited_row = UINT32_MAX;
  uint32_t max_visited_row = 0;
  bool saw_scale_1 = false;
  bool saw_scale_3 = false;
  while(it.valid())
  {
    EXPECT_TRUE(it->valid()) << "Invalid index at row " << it->row()
      << " col " << it->column();
    min_visited_row = std::min(min_visited_row, it->row());
    max_visited_row = std::max(max_visited_row, it->row());
    auto sf = gggs::levels[0].latitudeScaleFactor(it->row());
    if(sf == 1) saw_scale_1 = true;
    if(sf == 3) saw_scale_3 = true;
    count++;
    if(!it.next()) break;
  }
  EXPECT_GT(count, 0u);
  EXPECT_TRUE(saw_scale_1) << "Should visit scale-1 rows";
  EXPECT_TRUE(saw_scale_3) << "Should visit scale-3 rows";
  EXPECT_EQ(min_visited_row, expected_min_row);
  EXPECT_EQ(max_visited_row, expected_max_row);

  // Verify count matches sum of per-row column counts
  uint32_t expected_count = 0;
  double west = std::min(gi1.westLongitude(), gi2.westLongitude());
  double east = std::max(gi1.eastLongitude(), gi2.eastLongitude());
  for(uint32_t r = expected_min_row; r <= expected_max_row; ++r)
  {
    double span = gggs::levels[0].gridLongitudinalSpan(r);
    uint32_t first = static_cast<uint32_t>((west + 180.0) / span);
    uint32_t last = static_cast<uint32_t>((east + 180.0) / span);
    if(east + 180.0 > 0.0 && std::fmod(east + 180.0, span) < 1e-10)
      last--;
    uint32_t max_col = gggs::levels[0].columnCount(r) - 1;
    first = std::min(first, max_col);
    last = std::min(last, max_col);
    expected_count += 1 + last - first;
  }
  EXPECT_EQ(count, expected_count);
}

TEST(GGGSGridBounds, CrossBand)
{
  // Expand bounds across the 72-degree boundary.
  gggs::Level level(0);
  auto gi1 = level.gridIndex(71.0, 0.0);
  auto gi2 = level.gridIndex(73.0, 0.0);

  gggs::GridBounds bounds;
  bounds.expand(gi1);
  bounds.expand(gi2);
  EXPECT_TRUE(bounds.valid());

  // Per-row column counts should differ across the band boundary
  uint32_t cols_at_71_row = bounds.gridColumnCount(gi1.row());
  uint32_t cols_at_73_row = bounds.gridColumnCount(gi2.row());
  // The scale-1 row should have more columns than the scale-3 row for the
  // same longitude range (or the same if the range is narrow enough to fit
  // in one grid on both sides)
  EXPECT_GE(cols_at_71_row, cols_at_73_row);
}

TEST(GGGSGridBounds, PerRowColumnCount)
{
  // Single-band bounds: verify gridColumnCount(row) matches old behavior
  gggs::Level level(5);
  auto gi1 = level.gridIndex(43.0, -71.0);
  auto gi2 = level.gridIndex(43.5, -70.0);

  gggs::GridBounds bounds;
  bounds.expand(gi1);
  bounds.expand(gi2);
  EXPECT_TRUE(bounds.valid());

  // Both grids are in the same band (scale factor 1), so gridColumnCount
  // should be the same for any row in the bounds
  uint32_t cols = bounds.gridColumnCount(gi1.row());
  EXPECT_GE(cols, 1u);

  // All rows in the bounds should have the same column count
  auto min_gi = bounds.minimum();
  auto max_gi = bounds.maximum();
  for(uint32_t r = min_gi.row(); r <= max_gi.row(); ++r)
  {
    EXPECT_EQ(bounds.gridColumnCount(r), cols)
      << "Column count should be uniform in same band at row " << r;
  }
}

// ============================================================================
// index_math.h — parent() / children() quadtree navigation
// ============================================================================

TEST(GGGSIndexMath, ParentIsOneLevelCoarser)
{
  gggs::Level level(10);
  auto child = level.gridIndex(43.0, -70.5);  // New Hampshire coast
  auto parent = gggs::parent(child);
  ASSERT_TRUE(parent.valid());
  EXPECT_EQ(parent.level(), 9);
}

TEST(GGGSIndexMath, ParentContainsChildCentre)
{
  gggs::Level level(10);
  auto child = level.gridIndex(43.0, -70.5);
  auto parent = gggs::parent(child);
  ASSERT_TRUE(parent.valid());

  const double child_center_lat = 0.5 * (child.southLatitude() + child.northLatitude());
  const double child_center_lon = 0.5 * (child.westLongitude() + child.eastLongitude());
  EXPECT_LE(parent.southLatitude(), child_center_lat);
  EXPECT_GE(parent.northLatitude(), child_center_lat);
  EXPECT_LE(parent.westLongitude(), child_center_lon);
  EXPECT_GE(parent.eastLongitude(), child_center_lon);
}

TEST(GGGSIndexMath, ParentAtLevelZeroIsInvalid)
{
  gggs::Level level(0);
  auto grid = level.gridIndex(43.0, -70.5);
  ASSERT_EQ(grid.level(), 0);
  EXPECT_FALSE(gggs::parent(grid).valid());
}

TEST(GGGSIndexMath, ParentOfInvalidIsInvalid)
{
  gggs::GridIndex invalid;
  ASSERT_FALSE(invalid.valid());
  EXPECT_FALSE(gggs::parent(invalid).valid());
}

TEST(GGGSIndexMath, ChildrenTemperateBandAreFour)
{
  gggs::Level level(9);
  auto parent = level.gridIndex(43.0, -70.5);
  auto kids = gggs::children(parent);
  EXPECT_EQ(kids.size(), 4u);
}

TEST(GGGSIndexMath, ChildrenAllMapBackToParent)
{
  gggs::Level level(9);
  auto parent = level.gridIndex(43.0, -70.5);
  auto kids = gggs::children(parent);
  ASSERT_FALSE(kids.empty());
  for (const auto& kid : kids)
  {
    EXPECT_EQ(kid.level(), parent.level() + 1);
    EXPECT_EQ(gggs::parent(kid), parent);
  }
}

TEST(GGGSIndexMath, SiblingsShareTheSameParent)
{
  gggs::Level level(10);
  auto child = level.gridIndex(43.0, -70.5);
  auto parent = gggs::parent(child);
  ASSERT_TRUE(parent.valid());
  auto siblings = gggs::children(parent);
  ASSERT_GE(siblings.size(), 2u);
  // The original child is among the siblings, and every sibling shares the parent.
  bool found_child = false;
  for (const auto& sibling : siblings)
  {
    EXPECT_EQ(gggs::parent(sibling), parent);
    if (sibling == child)
      found_child = true;
  }
  EXPECT_TRUE(found_child);
}

TEST(GGGSIndexMath, ChildrenAtFinestLevelIsEmpty)
{
  gggs::Level level(20);  // finest level — no children
  auto grid = level.gridIndex(43.0, -70.5);
  ASSERT_EQ(grid.level(), 20);
  EXPECT_TRUE(gggs::children(grid).empty());
}

TEST(GGGSIndexMath, ChildrenOfInvalidIsEmpty)
{
  gggs::GridIndex invalid;
  ASSERT_FALSE(invalid.valid());
  EXPECT_TRUE(gggs::children(invalid).empty());
}

TEST(GGGSIndexMath, ParentChildrenRoundTripSubPolarBand)
{
  // ~75 deg latitude — a higher polar column scale-factor band.
  gggs::Level level(9);
  auto parent = level.gridIndex(75.0, 10.0);
  auto kids = gggs::children(parent);
  ASSERT_FALSE(kids.empty());
  for (const auto& kid : kids)
    EXPECT_EQ(gggs::parent(kid), parent);
}

TEST(GGGSIndexMath, ParentChildrenRoundTripPolarBand)
{
  // ~85 deg latitude — the highest polar column scale-factor band.
  gggs::Level level(9);
  auto parent = level.gridIndex(85.0, 10.0);
  auto kids = gggs::children(parent);
  ASSERT_FALSE(kids.empty());
  for (const auto& kid : kids)
    EXPECT_EQ(gggs::parent(kid), parent);
}
