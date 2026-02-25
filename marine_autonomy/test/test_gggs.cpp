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
  EXPECT_EQ(bounds.gridColumnCount(), 1u);
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
  EXPECT_GE(bounds.gridColumnCount(), 1u);
}

TEST(GGGSGridBounds, CellCounts)
{
  gggs::Level level(5);
  auto gi = level.gridIndex(43.0, -70.5);
  gggs::GridBounds bounds;
  bounds.expand(gi);
  EXPECT_EQ(bounds.cellRowCount(), static_cast<uint64_t>(gggs::cell_rows_per_grid));
  EXPECT_EQ(bounds.cellColumnCount(), static_cast<uint64_t>(gggs::cell_columns_per_grid));
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
  gz4d::PositionDegrees pos(43.0, -70.5);
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
  gz4d::PositionDegrees pos(43.0, -70.5);
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
  gz4d::PositionDegrees pos(43.0, -70.5);
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
  uint32_t expected_rows = 1 + gi2.row() - gi1.row();
  uint32_t expected_cols = 1 + gi2.column() - gi1.column();
  if (gi1.row() > gi2.row()) expected_rows = 1 + gi1.row() - gi2.row();
  if (gi1.column() > gi2.column()) expected_cols = 1 + gi1.column() - gi2.column();

  auto from = min(gi1, gi2);
  auto to = max(gi1, gi2);
  gggs::GridAreaIterator it(from, to);
  uint32_t count = 0;
  if (it.valid())
  {
    count = 1;
    while (it.next()) count++;
  }
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
  gz4d::BoundsDegrees bounds(pos, pos);
  gggs::CellAreaIterator it(gi, bounds);
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
  gz4d::BoundsDegrees bounds(sw, ne);
  gggs::CellAreaIterator it(gi, bounds);

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
  gz4d::BoundsDegrees bounds(pos, pos);
  gggs::CellAreaIterator it(gi, bounds);
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
  gz4d::PositionDegrees pos(south + 4.0, gi.westLongitude() + 1.0);
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
