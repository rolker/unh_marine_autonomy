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

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_sidescan_mosaic/processed_accumulator.hpp"

using marine_sidescan_mosaic::ProcessedAccumulator;

namespace
{
gggs::CellIndex aCell(const gggs::Level & level)
{
  geographic_msgs::msg::GeoPoint p;
  p.latitude = 43.07;
  p.longitude = -71.42;
  p.altitude = 0.0;
  return level.cellIndex(p);
}
}  // namespace

TEST(ProcessedAccumulator, BestSourceKeepsHighestQuality)
{
  const gggs::Level level(13);
  ProcessedAccumulator acc(level);
  const gggs::CellIndex cell = aCell(level);

  acc.add(cell, 100, 50, 1);   // first look
  acc.add(cell, 200, 80, 2);   // better quality -> wins
  acc.add(cell, 300, 30, 3);   // worse quality -> ignored
  acc.add(cell, 999, 80, 4);   // tie -> incumbent kept (strict >)

  ASSERT_EQ(acc.tiles().count(cell.grid()), 1u);
  const auto & tile = acc.tiles().at(cell.grid());
  const auto r = cell.row();
  const auto c = cell.column();
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kIntensity), 200);
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kQuality), 80);
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kSource), 2);
}

TEST(ProcessedAccumulator, TileHasThreeBands)
{
  const gggs::Level level(13);
  ProcessedAccumulator acc(level);
  acc.add(aCell(level), 10, 5, 1);
  const auto & tile = acc.tiles().begin()->second;
  EXPECT_EQ(tile.bandCount(), ProcessedAccumulator::kBands);
}

TEST(ProcessedAccumulator, InvalidCellIsNoOp)
{
  const gggs::Level level(13);
  ProcessedAccumulator acc(level);
  acc.add(gggs::CellIndex{}, 100, 100, 1);   // default cell is invalid
  EXPECT_TRUE(acc.tiles().empty());
}
