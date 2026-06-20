// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <gtest/gtest.h>

#include <cstdint>

#include "marine_autonomy/gggs.h"
#include "marine_sidescan_mosaic/accumulator.hpp"

namespace msm = marine_sidescan_mosaic;

namespace
{
const gggs::Level kLevel(13);

// A CellIndex at a deterministic (grid, row, col) near Lake Massabesic.
gggs::CellIndex cellAt(uint16_t row, uint16_t col, double lat = 43.07, double lon = -71.42)
{
  return gggs::CellIndex(kLevel.gridIndex(lat, lon), row, col);
}

std::uint16_t valueAt(const msm::MosaicAccumulator & acc, const gggs::CellIndex & cell)
{
  return acc.tiles().at(cell.grid()).get(cell.row(), cell.column(), 0);
}
}  // namespace

TEST(Accumulator, MeanOfSamples)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::Mean);
  const auto cell = cellAt(10, 20);
  acc.add(cell, 10);
  acc.add(cell, 20);
  acc.add(cell, 30);
  EXPECT_EQ(valueAt(acc, cell), 20);          // (10+20+30)/3
  acc.add(cell, 40);
  EXPECT_EQ(valueAt(acc, cell), 25);          // 100/4
}

TEST(Accumulator, MeanTruncates)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::Mean);
  const auto cell = cellAt(5, 5);
  acc.add(cell, 10);
  acc.add(cell, 15);
  EXPECT_EQ(valueAt(acc, cell), 12);          // 25/2 = 12.5 -> 12 (integer truncation)
}

TEST(Accumulator, MaxHold)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::MaxHold);
  const auto cell = cellAt(1, 1);
  acc.add(cell, 10);
  acc.add(cell, 30);
  acc.add(cell, 20);
  EXPECT_EQ(valueAt(acc, cell), 30);          // brightest wins, order-independent
}

TEST(Accumulator, IndependentCellsAndGrids)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::Mean);
  const auto a = cellAt(1, 1);
  const auto b = cellAt(2, 2);
  const auto far = cellAt(1, 1, 43.50, -71.10);   // a different grid
  acc.add(a, 100);
  acc.add(b, 200);
  acc.add(far, 50);
  EXPECT_EQ(valueAt(acc, a), 100);
  EXPECT_EQ(valueAt(acc, b), 200);
  EXPECT_EQ(valueAt(acc, far), 50);
  ASSERT_NE(a.grid(), far.grid());
  EXPECT_EQ(acc.tiles().size(), 2u);              // two grids touched
}

// The uint64 sum must not overflow even at the uint16 ceiling over many samples.
TEST(Accumulator, NoOverflowAtCeiling)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::Mean);
  const auto cell = cellAt(7, 7);
  for (int i = 0; i < 1000; ++i) {
    acc.add(cell, 65535);
  }
  EXPECT_EQ(valueAt(acc, cell), 65535);           // mean of all-max is max, no wrap
}

TEST(Accumulator, TileDirtyAfterAdd)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::Mean);
  const auto cell = cellAt(3, 3);
  acc.add(cell, 42);
  EXPECT_TRUE(acc.tiles().at(cell.grid()).dirty());
}

// An untouched cell reads back 0 (no-data) even once its tile exists.
TEST(Accumulator, UntouchedCellIsZero)
{
  msm::MosaicAccumulator acc(kLevel, msm::SplatPolicy::Mean);
  const auto written = cellAt(4, 4);
  const auto untouched = cellAt(8, 9);            // same grid, different cell
  acc.add(written, 123);
  EXPECT_EQ(valueAt(acc, untouched), 0);
}
