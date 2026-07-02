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

#include "geographic_msgs/msg/geo_point.hpp"
#include "marine_autonomy/gggs.h"
#include "marine_backscatter/processed_accumulator.hpp"

using marine_backscatter::ProcessedAccumulator;

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

// foldTile: a reloaded store tile with a HIGHER-quality look wins the shared
// cell, and a cell present only in the store survives the merge (issue #253).
TEST(ProcessedAccumulator, FoldTileBestSourceMerge)
{
  const gggs::Level level(13);
  const gggs::CellIndex cell = aCell(level);
  const auto r = cell.row();
  const auto c = cell.column();
  const std::uint16_t r2 = (r == 0) ? 1u : static_cast<std::uint16_t>(r - 1);

  ProcessedAccumulator acc(level);
  acc.add(cell, 100, 50, 1);   // this run's look at the shared cell, quality 50

  ProcessedAccumulator::Tile existing(cell.grid(), ProcessedAccumulator::kBands, 0);
  existing.set(r, c, ProcessedAccumulator::kIntensity, 200);
  existing.set(r, c, ProcessedAccumulator::kQuality, 90);   // better -> wins
  existing.set(r, c, ProcessedAccumulator::kSource, 7);
  existing.set(r2, c, ProcessedAccumulator::kIntensity, 300);
  existing.set(r2, c, ProcessedAccumulator::kQuality, 20);   // store-only cell
  existing.set(r2, c, ProcessedAccumulator::kSource, 8);

  acc.foldTile(existing);

  const auto & tile = acc.tiles().at(cell.grid());
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kIntensity), 200);
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kQuality), 90);
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kSource), 7);
  EXPECT_EQ(tile.get(r2, c, ProcessedAccumulator::kQuality), 20);
  EXPECT_EQ(tile.get(r2, c, ProcessedAccumulator::kSource), 8);
}

// foldTile: a worse-quality reloaded cell is ignored, and no-data (quality 0)
// cells in the reloaded tile are skipped (they never create spurious cells).
TEST(ProcessedAccumulator, FoldTileKeepsBetterIncumbentAndSkipsNoData)
{
  const gggs::Level level(13);
  const gggs::CellIndex cell = aCell(level);
  const auto r = cell.row();
  const auto c = cell.column();

  ProcessedAccumulator acc(level);
  acc.add(cell, 100, 80, 1);   // this run holds quality 80 at the shared cell

  ProcessedAccumulator::Tile existing(cell.grid(), ProcessedAccumulator::kBands, 0);
  existing.set(r, c, ProcessedAccumulator::kIntensity, 200);
  existing.set(r, c, ProcessedAccumulator::kQuality, 30);   // worse -> ignored
  existing.set(r, c, ProcessedAccumulator::kSource, 9);
  // every other cell stays quality 0 (no-data) and must be skipped.

  acc.foldTile(existing);

  const auto & tile = acc.tiles().at(cell.grid());
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kIntensity), 100);   // incumbent kept
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kQuality), 80);
  EXPECT_EQ(tile.get(r, c, ProcessedAccumulator::kSource), 1);
}
